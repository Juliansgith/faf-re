// Engine addition (faf-neural-ai): see LiveLaunch.h.

#include "headless/LiveLaunch.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

#include "legacy/containers/String.h"
#include "legacy/containers/Vector.h"
#include "moho/misc/StartupHelpers.h"

#include "lua/LuaIncrementalGc.h"
#include "lua/LuaObject.h"
#include "lua/LuaPrimitives.h"
#include "moho/lua/CScrLuaInitForm.h"
#include "moho/misc/SessionStartup.h"
#include "moho/sim/WldSessionInfo.h"

namespace faf::headless
{
  namespace
  {
    // The front end's GetCommandLineArg and HasCommandLineArg (moho/app/StartupClientHelpers.cpp, user
    // bindings the Linux build does not link), on the same CFG_GetArgOption: a table of up to `count`
    // values, or false; whether the switch is there.
    int LaunchGetCommandLineArg(lua_State* const L)
    {
      const char* const option = lua_tostring(L, 1);
      const int count = static_cast<int>(lua_tonumber(L, 2));
      fa::vector<fa::string> values;
      if (option == nullptr || !moho::CFG_GetArgOption(option, static_cast<std::uint32_t>(count), &values)) {
        lua_pushboolean(L, 0);
        return 1;
      }
      lua_newtable(L);
      const int capped = std::min(count, static_cast<int>(values.size()));
      for (int index = 0; index < capped; ++index) {
        lua_pushnumber(L, static_cast<lua_Number>(index + 1));
        lua_pushstring(L, values[static_cast<std::size_t>(index)].c_str());
        lua_settable(L, -3);
      }
      return 1;
    }

    int LaunchHasCommandLineArg(lua_State* const L)
    {
      const char* const option = lua_tostring(L, 1);
      fa::vector<fa::string> values;
      lua_pushboolean(L, option != nullptr && moho::CFG_GetArgOption(option, 0, &values) ? 1 : 0);
      return 1;
    }

    void SetGlobalFunction(lua_State* const L, const char* const name, const lua_CFunction function)
    {
      lua_pushstring(L, name);
      lua_pushcclosure(L, function, 0);
      lua_settable(L, LUA_GLOBALSINDEX);
    }
  } // namespace

  fa::auto_ptr<moho::SWldSessionInfo> BuildLiveSession(const std::string& launchFile, std::string& error)
  {
    // FAF's front end runs its scripts in a state with the core and user bindings; the launch file needs only
    // the core ones (doscript, DiskFindFiles, DiskGetFileInfo, the command line, logging).
    std::unique_ptr<LuaPlus::LuaState> state(
      new LuaPlus::LuaState(LuaPlus::LuaState::LIB_BASE, static_cast<int>(moho::lua_incremental::ELuaVmRole::FrontendUi))
    );
    if (moho::CScrLuaInitFormSet* const core = moho::SCR_FindLuaInitFormSet("Core"); core != nullptr) {
      core->RunInits(state.get());
    }
    lua_State* const L = state->m_state;
    SetGlobalFunction(L, "GetCommandLineArg", &LaunchGetCommandLineArg);
    SetGlobalFunction(L, "HasCommandLineArg", &LaunchHasCommandLineArg);

    // The same steps as the Windows hook's StartCommandLineSession (schook/lua/SinglePlayerLaunch.lua).
    // Lua 5.0 has no chunk-level `...`: the path goes in as a long string.
    if (launchFile.find("]]") != std::string::npos) {
      error = "the launch file's path may not contain ]]";
      return fa::auto_ptr<moho::SWldSessionInfo>(nullptr);
    }
    const std::string script = "local file = [[" + launchFile + "]]\n"
                               "local env = setmetatable({}, { __index = _G })\n"
                               "doscript(file, env)\n"
                               "if type(env.Build) ~= 'function' then error(file .. ' defines no Build()') end\n"
                               "return env.Build()\n";
    if (luaL_loadbuffer(L, script.data(), script.size(), "=agentlaunch") != 0) {
      error = lua_tostring(L, -1) != nullptr ? lua_tostring(L, -1) : "the launch chunk does not compile";
      return fa::auto_ptr<moho::SWldSessionInfo>(nullptr);
    }
    if (LuaCallProtected(L, 0, 1) != 0) { // this Lua has no lua_pcall (lua/LuaPrimitives.h)
      const char* const message = lua_tostring(L, -1);
      error = std::string("the launch file failed: ") + (message != nullptr ? message : "(no message)");
      return fa::auto_ptr<moho::SWldSessionInfo>(nullptr);
    }
    if (lua_type(L, -1) != LUA_TTABLE) {
      error = launchFile + ": Build() returned no table";
      return fa::auto_ptr<moho::SWldSessionInfo>(nullptr);
    }
    const LuaPlus::LuaObject launchData(LuaPlus::LuaStackObject(state.get(), lua_gettop(L)));
    // The session keeps the table's contents as strings (scenario, mods, one per army): the state can go.
    return moho::WLD_SetupSessionInfo(launchData);
  }
} // namespace faf::headless
