// Engine addition (faf-neural-ai): the native Linux client plays online through the lockstep transport
// (moho/net/CNet*.cpp on platform/posix/WinsockShim.cpp) - direct games, LAN discovery, and FAF through
// its ICE adapter over GPGNet - but leaves out the in-engine peer links, moho/net/NetlinkPeers.cpp
// (src/platform/sdl/sources.cmake), whose ICE stack - src/netlink on libjuice and libdatachannel - the
// Linux build does not link. What the rest of the client calls of them: no peer links, empty snapshots,
// Netlink* commands dropped (said once in the log).

#include <atomic>

#include "gpg/core/utils/Logging.h"
#include "moho/net/NetlinkPeers.h"

namespace
{
  void LogOnce(std::atomic<bool>& logged, const char* const what)
  {
    if (!logged.exchange(true)) {
      gpg::Warnf("%s: the in-engine peer links are not in the Linux client yet", what);
    }
  }
} // namespace

moho::SNetlinkSnapshot moho::NETLINK_GetSnapshot()
{
  return {};
}

void moho::NETLINK_KeepLogLines() {}

bool moho::NETLINK_ReconnectPeer(std::int64_t, const faf::netlink::PathChoice*)
{
  return false;
}

void moho::NETLINK_SayHello(CGpgNetInterface&) {}

bool moho::NETLINK_HandleCommand(const fa::string& name, const fa::vector<SNetCommandArg>&)
{
  if (name.size() >= 7 && std::char_traits<char>::compare(name.c_str(), "Netlink", 7) == 0) {
    static std::atomic<bool> sLogged{false};
    LogOnce(sLogged, "NETLINK_HandleCommand (in-engine peer links)");
    return true; // a Netlink* command, dropped
  }
  return false;
}

void moho::NETLINK_Shutdown() {}

bool moho::NATIVENET_Enabled()
{
  return false;
}

void moho::NATIVENET_Start() {}

bool moho::NATIVENET_HandleCommand(const fa::string&, fa::vector<SNetCommandArg>&)
{
  return false;
}

void moho::NATIVENET_OnGameMessage(const char*) {}
