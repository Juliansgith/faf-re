// Engine addition (faf-neural-ai): Winsock's event model on POSIX, for the Linux build's lockstep
// transport (moho/net/CNetUDPConnector.cpp, CNetTCPConnector.cpp, CNetDatagramSocketImpl.cpp) and its
// GPGNet link, which run unchanged on top of it.
//
// WSAEventSelect(socket, event, FD_*) hands the socket to one watcher thread, which poll(2)s every
// selected socket and sets the socket's event when one of its selected network events is pending:
//   FD_READ / FD_ACCEPT / FD_CLOSE  the socket is readable (data, a connection to accept, the peer's
//                                   close or an error). Winsock reports these again after the program
//                                   reads; here the watcher looks again whenever the event goes back to
//                                   not signalled (reset, or taken by a wait), and sets it again while
//                                   the socket is still readable. A signalled event's sockets are not
//                                   polled for reading, so an unread socket costs no spinning.
//   FD_CONNECT                      once, when a non-blocking connect completes (or fails).
//   FD_WRITE                        once when selected on a socket that can send (a bound datagram
//                                   socket, a connected stream), and once after FD_CONNECT. Winsock
//                                   reports it again only after a send failed with WSAEWOULDBLOCK; no
//                                   engine code waits for that, so it is not reported again here.
// What the engine waits on is the event: WSAWaitForMultipleEvents and the Win32 waits (Win32Shim.cpp).

#include <winsock2.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "platform/posix/WinsockShimHooks.h"

namespace
{
  struct Selection
  {
    HANDLE event = nullptr;
    long events = 0;
    bool listening = false;     // FD_ACCEPT's socket
    bool connectPending = false; // a stream socket whose connect has not completed: FD_CONNECT
    std::uint64_t serial = 0;   // tells a new selection of a reused descriptor from the old one
  };

  class Watcher
  {
  public:
    static Watcher& Instance()
    {
      // Never destroyed: the thread runs for the life of the process.
      static Watcher* const sWatcher = [] {
        auto* const watcher = new Watcher();
        sStarted.store(watcher, std::memory_order_release);
        return watcher;
      }();
      return *sWatcher;
    }

    /// The watcher if a socket was ever selected: closing other sockets starts no thread.
    static Watcher* IfStarted() noexcept
    {
      return sStarted.load(std::memory_order_acquire);
    }

    int Select(const SOCKET socket, const HANDLE event, const long events)
    {
      if (event == nullptr || events == 0) {
        Forget(socket);
        return 0;
      }

      const int flags = ::fcntl(socket, F_GETFL, 0);
      if (flags < 0 || ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        return SOCKET_ERROR;
      }

      int type = 0;
      socklen_t typeLength = sizeof(type);
      (void)::getsockopt(socket, SOL_SOCKET, SO_TYPE, &type, &typeLength);
      int accepting = 0;
      socklen_t acceptingLength = sizeof(accepting);
      (void)::getsockopt(socket, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &acceptingLength);
      sockaddr_storage peer{};
      socklen_t peerLength = sizeof(peer);
      const bool connected = ::getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &peerLength) == 0;

      Selection selection{};
      selection.event = event;
      selection.events = events;
      selection.listening = accepting != 0;
      selection.connectPending = type == SOCK_STREAM && !selection.listening && !connected && (events & FD_CONNECT) != 0;

      FafShimWatchEvent(event);
      {
        const std::lock_guard<std::mutex> lock(mLock);
        selection.serial = ++mSerial;
        mSelections[socket] = selection;
      }
      // FD_WRITE: a socket that can send says so at once, as Winsock does.
      if ((events & FD_WRITE) != 0 && (type == SOCK_DGRAM || connected)) {
        (void)::SetEvent(event);
      }
      Wake();
      return 0;
    }

    void Forget(const SOCKET socket)
    {
      {
        const std::lock_guard<std::mutex> lock(mLock);
        if (mSelections.erase(socket) == 0) {
          return;
        }
      }
      Wake();
    }

    void ForgetEvent(const HANDLE event)
    {
      const std::lock_guard<std::mutex> lock(mLock);
      for (auto it = mSelections.begin(); it != mSelections.end();) {
        it = it->second.event == event ? mSelections.erase(it) : std::next(it);
      }
    }

    void Wake() noexcept
    {
      const std::uint64_t one = 1;
      (void)!::write(mWakeFd, &one, sizeof(one));
    }

  private:
    Watcher()
      : mWakeFd(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
    {
      std::thread([this] { Run(); }).detach();
    }

    struct Polled
    {
      SOCKET socket;
      std::uint64_t serial;
    };

    void Run()
    {
      pthread_setname_np(pthread_self(), "WSAEventSelect");
      std::vector<pollfd> fds;
      std::vector<Polled> polled;
      for (;;) {
        fds.clear();
        polled.clear();
        fds.push_back(pollfd{mWakeFd, POLLIN, 0});
        {
          const std::lock_guard<std::mutex> lock(mLock);
          for (const auto& [socket, selection] : mSelections) {
            short wanted = 0;
            if ((selection.events & (FD_READ | FD_ACCEPT | FD_CLOSE)) != 0 && !FafShimEventSignalled(selection.event)) {
              wanted |= POLLIN;
            }
            if (selection.connectPending) {
              wanted |= POLLOUT;
            }
            if (wanted != 0) {
              fds.push_back(pollfd{socket, wanted, 0});
              polled.push_back(Polled{socket, selection.serial});
            }
          }
        }

        // The timeout only bounds how long a missed wake could matter.
        const int ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 250);
        if (ready < 0 && errno != EINTR) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          continue;
        }
        if ((fds[0].revents & POLLIN) != 0) {
          std::uint64_t count = 0;
          (void)!::read(mWakeFd, &count, sizeof(count));
        }
        if (ready <= 0) {
          continue;
        }

        const std::lock_guard<std::mutex> lock(mLock);
        for (std::size_t index = 1; index < fds.size(); ++index) {
          const short revents = fds[index].revents;
          if (revents == 0) {
            continue;
          }
          const auto found = mSelections.find(polled[index - 1].socket);
          if (found == mSelections.end() || found->second.serial != polled[index - 1].serial) {
            continue; // closed or selected again meanwhile
          }
          Selection& selection = found->second;
          bool signal = false;
          if ((revents & POLLNVAL) != 0) {
            mSelections.erase(found); // the descriptor was closed without closesocket
            continue;
          }
          if ((fds[index].events & POLLIN) != 0 && (revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            signal = true;
          }
          if (selection.connectPending && (revents & (POLLOUT | POLLHUP | POLLERR)) != 0) {
            selection.connectPending = false;
            signal = true;
          }
          if (signal) {
            // Under the lock: WSACloseEvent / CloseHandle forget the event under it before it goes.
            (void)::SetEvent(selection.event);
          }
        }
      }
    }

    static inline std::atomic<Watcher*> sStarted{nullptr};

    const int mWakeFd;
    std::mutex mLock;
    std::unordered_map<SOCKET, Selection> mSelections;
    std::uint64_t mSerial = 0;
  };
} // namespace

extern "C" void FafWsaForgetSocket(const int socket)
{
  if (Watcher* const watcher = Watcher::IfStarted(); watcher != nullptr) {
    watcher->Forget(socket);
  }
}

void FafWsaEventCleared() noexcept
{
  if (Watcher* const watcher = Watcher::IfStarted(); watcher != nullptr) {
    watcher->Wake();
  }
}

void FafWsaForgetEvent(const HANDLE handle) noexcept
{
  if (Watcher* const watcher = Watcher::IfStarted(); watcher != nullptr) {
    watcher->ForgetEvent(handle);
  }
}

WSAEVENT WSACreateEvent()
{
  return ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

BOOL WSACloseEvent(const WSAEVENT event)
{
  return ::CloseHandle(event);
}

BOOL WSASetEvent(const WSAEVENT event)
{
  return ::SetEvent(event);
}

BOOL WSAResetEvent(const WSAEVENT event)
{
  return ::ResetEvent(event);
}

int WSAEventSelect(const SOCKET socket, const WSAEVENT event, const long networkEvents)
{
  return Watcher::Instance().Select(socket, event, networkEvents);
}

DWORD WSAWaitForMultipleEvents(
  const DWORD count, const WSAEVENT* const events, const BOOL waitAll, const DWORD milliseconds, const BOOL alertable
)
{
  if (count == 1) {
    return ::WaitForSingleObjectEx(events[0], milliseconds, alertable);
  }
  return ::WaitForMultipleObjects(count, events, waitAll, milliseconds);
}
