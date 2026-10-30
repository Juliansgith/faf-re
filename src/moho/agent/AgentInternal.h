#pragma once

#include <cstddef>
#include <string>

struct lua_State;

// Engine addition (agent bridge): helpers shared by AgentBridge.cpp and AgentObserve.cpp. Not part of
// the recovered engine.
namespace moho::agent
{
  /** JSON string literal of `text` (escaped) appended to `out`. */
  void AppendJsonString(std::string& out, const char* text, std::size_t length);

  /**
   * JSON number appended to `out`, formatted like the bridge formats Lua numbers: integers exactly,
   * anything else as the shortest float that round-trips (sim numbers are single precision); NaN and
   * infinities become null.
   */
  void AppendJsonNumber(std::string& out, double value);

  /** The Lua value at `index` as JSON (tables: 1..n arrays or string-keyed objects). */
  void EncodeLuaValue(lua_State* L, int index, std::string& out, int depth);

  /**
   * AgentObserve.cpp: registers `AgentBridge_ObserveUnits` in the sim Lua state `L` (a native build of
   * the observation's unit list; see AgentObserve.cpp) and forgets per-session caches of an earlier state.
   */
  void RegisterObserveFunctions(lua_State* L);

  /**
   * If the value at `index` is the token `AgentBridge_ObserveUnits` returns, appends the JSON it stands
   * for and returns true (the encoder splices pre-built JSON in place of a Lua table).
   */
  bool AppendRawJsonToken(lua_State* L, int index, std::string& out);
} // namespace moho::agent
