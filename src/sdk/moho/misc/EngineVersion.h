#pragma once

// The game version the engine reports: GetVersion() in Lua, and the "Supreme Commander v1.50.NNNN"
// header that replays are written with and must match to be played back. The recovered binary says
// 3835 (0xEFB); FAF stamps its own release number into every copy it ships (the 3839 executable pushes 0xEFF
// in GetEngineVersion and in VCR_SetupReplaySession), so a build meant to run with newer FAF data sets
// the MSBuild property FafEngineVersion, which defines FAF_ENGINE_VERSION for the files that use it.
#ifndef FAF_ENGINE_VERSION
#define FAF_ENGINE_VERSION 3835
#endif

namespace moho
{
  inline constexpr int kEngineBuildVersion = FAF_ENGINE_VERSION;
} // namespace moho
