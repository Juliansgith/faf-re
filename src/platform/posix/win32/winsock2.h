#pragma once

// Engine addition (dedicated-server plan WPL.5): the subset of <winsock2.h> that the shared
// network code names, for the Linux build, on BSD sockets. Winsock's event model, which the
// lockstep transport (moho/net/CNet*.cpp) runs on - WSAEventSelect tying a socket's network
// events to an event the engine waits on, WSAWaitForMultipleEvents - is implemented on
// poll(2) in src/platform/posix/WinsockShim.cpp (faf-neural-ai, the Linux client's online play).

#if defined(_WIN32)
#error "platform/posix/win32/winsock2.h is for builds without the Windows SDK"
#endif

#include <Windows.h>

#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

using SOCKET = int;
using WSAEVENT = HANDLE;
using SOCKADDR = sockaddr;
using SOCKADDR_IN = sockaddr_in;
using ADDRINFOA = addrinfo;
using PADDRINFOA = addrinfo*;
using u_long = unsigned long;
using u_short = unsigned short;

#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR

struct WSADATA
{
  WORD wVersion;
  WORD wHighVersion;
  char szDescription[257];
  char szSystemStatus[129];
  unsigned short iMaxSockets;
  unsigned short iMaxUdpDg;
  char* lpVendorInfo;
};

inline int WSAStartup(WORD, WSADATA* const data) noexcept
{
  if (data != nullptr) {
    *data = WSADATA{};
    data->wVersion = data->wHighVersion = 0x0101;
  }
  return 0;
}

inline int WSACleanup() noexcept
{
  return 0;
}

using WSAData = WSADATA;

/// platform/posix/WinsockShim.cpp, when it is linked: drops the socket from WSAEventSelect's watch
/// before its descriptor is closed (and possibly reused).
extern "C" void FafWsaForgetSocket(int socket) __attribute__((weak));

inline int closesocket(const SOCKET socket) noexcept
{
  if (FafWsaForgetSocket != nullptr) {
    FafWsaForgetSocket(socket);
  }
  return ::close(socket);
}

// Winsock's length arguments are int; BSD's are socklen_t. These overloads take the Winsock form.
inline int getsockname(const SOCKET socket, sockaddr* const name, int* const length) noexcept
{
  socklen_t bsdLength = length != nullptr ? static_cast<socklen_t>(*length) : 0;
  const int result = ::getsockname(socket, name, &bsdLength);
  if (length != nullptr) {
    *length = static_cast<int>(bsdLength);
  }
  return result;
}

inline int getpeername(const SOCKET socket, sockaddr* const name, int* const length) noexcept
{
  socklen_t bsdLength = length != nullptr ? static_cast<socklen_t>(*length) : 0;
  const int result = ::getpeername(socket, name, &bsdLength);
  if (length != nullptr) {
    *length = static_cast<int>(bsdLength);
  }
  return result;
}

inline SOCKET accept(const SOCKET socket, sockaddr* const address, int* const length) noexcept
{
  socklen_t bsdLength = length != nullptr ? static_cast<socklen_t>(*length) : 0;
  const SOCKET result = ::accept(socket, address, length != nullptr ? &bsdLength : nullptr);
  if (length != nullptr) {
    *length = static_cast<int>(bsdLength);
  }
  return result;
}

inline int recvfrom(
  const SOCKET socket, char* const buffer, const int length, const int flags, sockaddr* const from, int* const fromLength
) noexcept
{
  socklen_t bsdLength = fromLength != nullptr ? static_cast<socklen_t>(*fromLength) : 0;
  const auto result = ::recvfrom(
    socket, buffer, static_cast<std::size_t>(length), flags, from, fromLength != nullptr ? &bsdLength : nullptr
  );
  if (fromLength != nullptr) {
    *fromLength = static_cast<int>(bsdLength);
  }
  return static_cast<int>(result);
}

inline int getsockopt(const SOCKET socket, const int level, const int name, char* const value, int* const length) noexcept
{
  socklen_t bsdLength = length != nullptr ? static_cast<socklen_t>(*length) : 0;
  const int result = ::getsockopt(socket, level, name, value, &bsdLength);
  if (length != nullptr) {
    *length = static_cast<int>(bsdLength);
  }
  return result;
}

// --- Winsock's event model (src/platform/posix/WinsockShim.cpp) ---------------------------
// WSAEventSelect: a watcher thread polls the socket and sets the event when one of the selected
// network events is pending, as Winsock does. FD_READ, FD_ACCEPT and FD_CLOSE are reported again
// while data (or a connection, or the close) is still waiting once the event has been reset or
// taken by a wait; FD_CONNECT once when a connect completes; FD_WRITE once when selected on a
// writable socket (and with FD_CONNECT). WSAEventSelect makes the socket non-blocking.

#define FD_READ 0x01
#define FD_WRITE 0x02
#define FD_OOB 0x04
#define FD_ACCEPT 0x08
#define FD_CONNECT 0x10
#define FD_CLOSE 0x20

#define WSA_INVALID_EVENT (static_cast<WSAEVENT>(nullptr))
#define WSA_INFINITE INFINITE
#define WSA_WAIT_EVENT_0 WAIT_OBJECT_0
#define WSA_WAIT_IO_COMPLETION WAIT_IO_COMPLETION
#define WSA_WAIT_TIMEOUT WAIT_TIMEOUT
#define WSA_WAIT_FAILED WAIT_FAILED
#define WSA_MAXIMUM_WAIT_EVENTS 64

/// A manual-reset event, not signalled.
WSAEVENT WSACreateEvent();
BOOL WSACloseEvent(WSAEVENT event);
BOOL WSASetEvent(WSAEVENT event);
BOOL WSAResetEvent(WSAEVENT event);
/// Ties `socket`'s `networkEvents` (FD_*) to `event`, replacing an earlier selection; a null event
/// or no events cancels it.
int WSAEventSelect(SOCKET socket, WSAEVENT event, long networkEvents);
DWORD WSAWaitForMultipleEvents(DWORD count, const WSAEVENT* events, BOOL waitAll, DWORD milliseconds, BOOL alertable);

inline int ioctlsocket(const SOCKET socket, const unsigned long command, u_long* const argument) noexcept
{
  int value = argument != nullptr ? static_cast<int>(*argument) : 0;
  const int result = ::ioctl(socket, command, &value);
  if (argument != nullptr) {
    *argument = static_cast<u_long>(value);
  }
  return result;
}

// Winsock's error codes are its own numbers; WSAGetLastError reports errno mapped to them.
#define WSABASEERR 10000
#define WSAEINTR (WSABASEERR + 4)
#define WSAEBADF (WSABASEERR + 9)
#define WSAEACCES (WSABASEERR + 13)
#define WSAEFAULT (WSABASEERR + 14)
#define WSAEINVAL (WSABASEERR + 22)
#define WSAEMFILE (WSABASEERR + 24)
#define WSAEWOULDBLOCK (WSABASEERR + 35)
#define WSAEINPROGRESS (WSABASEERR + 36)
#define WSAEALREADY (WSABASEERR + 37)
#define WSAENOTSOCK (WSABASEERR + 38)
#define WSAEDESTADDRREQ (WSABASEERR + 39)
#define WSAEMSGSIZE (WSABASEERR + 40)
#define WSAEPROTOTYPE (WSABASEERR + 41)
#define WSAENOPROTOOPT (WSABASEERR + 42)
#define WSAEPROTONOSUPPORT (WSABASEERR + 43)
#define WSAESOCKTNOSUPPORT (WSABASEERR + 44)
#define WSAEOPNOTSUPP (WSABASEERR + 45)
#define WSAEPFNOSUPPORT (WSABASEERR + 46)
#define WSAEAFNOSUPPORT (WSABASEERR + 47)
#define WSAEADDRINUSE (WSABASEERR + 48)
#define WSAEADDRNOTAVAIL (WSABASEERR + 49)
#define WSAENETDOWN (WSABASEERR + 50)
#define WSAENETUNREACH (WSABASEERR + 51)
#define WSAENETRESET (WSABASEERR + 52)
#define WSAECONNABORTED (WSABASEERR + 53)
#define WSAECONNRESET (WSABASEERR + 54)
#define WSAENOBUFS (WSABASEERR + 55)
#define WSAEISCONN (WSABASEERR + 56)
#define WSAENOTCONN (WSABASEERR + 57)
#define WSAESHUTDOWN (WSABASEERR + 58)
#define WSAETOOMANYREFS (WSABASEERR + 59)
#define WSAETIMEDOUT (WSABASEERR + 60)
#define WSAECONNREFUSED (WSABASEERR + 61)
#define WSAELOOP (WSABASEERR + 62)
#define WSAENAMETOOLONG (WSABASEERR + 63)
#define WSAEHOSTDOWN (WSABASEERR + 64)
#define WSAEHOSTUNREACH (WSABASEERR + 65)
#define WSAENOTEMPTY (WSABASEERR + 66)
#define WSAEPROCLIM (WSABASEERR + 67)
#define WSAEUSERS (WSABASEERR + 68)
#define WSAEDQUOT (WSABASEERR + 69)
#define WSAESTALE (WSABASEERR + 70)
#define WSAEREMOTE (WSABASEERR + 71)
#define WSASYSNOTREADY (WSABASEERR + 91)
#define WSAVERNOTSUPPORTED (WSABASEERR + 92)
#define WSANOTINITIALISED (WSABASEERR + 93)
#define WSAEDISCON (WSABASEERR + 101)
#define WSAHOST_NOT_FOUND (WSABASEERR + 1001)
#define WSATRY_AGAIN (WSABASEERR + 1002)
#define WSANO_RECOVERY (WSABASEERR + 1003)
#define WSANO_DATA (WSABASEERR + 1004)

inline int WSAGetLastError() noexcept
{
  switch (errno) {
  case EINTR:
    return WSAEINTR;
  case EBADF:
    return WSAEBADF;
  case EACCES:
    return WSAEACCES;
  case EFAULT:
    return WSAEFAULT;
  case EINVAL:
    return WSAEINVAL;
  case EMFILE:
    return WSAEMFILE;
  case EWOULDBLOCK:
    return WSAEWOULDBLOCK;
  case EINPROGRESS:
    // A non-blocking connect that has not finished: Winsock reports it as WSAEWOULDBLOCK
    // (its WSAEINPROGRESS is a blocking call in progress, which BSD sockets do not have).
    return WSAEWOULDBLOCK;
  case EALREADY:
    return WSAEALREADY;
  case ENOTSOCK:
    return WSAENOTSOCK;
  case EMSGSIZE:
    return WSAEMSGSIZE;
  case EADDRINUSE:
    return WSAEADDRINUSE;
  case EADDRNOTAVAIL:
    return WSAEADDRNOTAVAIL;
  case ENETDOWN:
    return WSAENETDOWN;
  case ENETUNREACH:
    return WSAENETUNREACH;
  case ECONNABORTED:
    return WSAECONNABORTED;
  case ECONNRESET:
    return WSAECONNRESET;
  case ENOBUFS:
    return WSAENOBUFS;
  case EISCONN:
    return WSAEISCONN;
  case ENOTCONN:
    return WSAENOTCONN;
  case ETIMEDOUT:
    return WSAETIMEDOUT;
  case ECONNREFUSED:
    return WSAECONNREFUSED;
  case EHOSTUNREACH:
    return WSAEHOSTUNREACH;
  default:
    return errno;
  }
}
