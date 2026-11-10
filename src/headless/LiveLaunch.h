#pragma once

#include <string>

#include "legacy/containers/AutoPtr.h"

namespace moho
{
  struct SWldSessionInfo;
} // namespace moho

// Engine addition (faf-neural-ai): a live single-player game in headless, for the agent bridge's training
// games on Linux (and anywhere headless runs). `headless /agentlaunch <file> /map ... /agentbridge <port>`
// plays the game a Windows `main.exe /map ... /agentlaunch <file>` plays: both build the launch table with
// that Lua file's Build() (faf-neural-ai: ai/fa_overrides/schook/lua/AgentLaunch.lua) from the same
// switches, and both turn it into a session with WLD_SetupSessionInfo (moho/misc/SinglePlayerSessionInfo.cpp).

namespace faf::headless
{
  /**
   * The session of `/agentlaunch <file>`: the file's Build() run in a Lua state of its own with the engine's
   * core bindings (what FAF's front end has before its own scripts), its table made a single-player session
   * as `LaunchSinglePlayerSession` makes it. Null, with `error` set, when the file fails.
   */
  [[nodiscard]] fa::auto_ptr<moho::SWldSessionInfo> BuildLiveSession(const std::string& launchFile, std::string& error);
} // namespace faf::headless
