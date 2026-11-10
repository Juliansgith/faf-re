// Engine addition (faf-neural-ai): the single-player session's setup, moved here from SessionStartup.cpp
// unchanged so that the Linux build, which has no front end (SessionStartup.cpp is client-only), builds a
// training game's session the same way: `headless /agentlaunch` (src/headless/LiveLaunch.cpp).

#include "moho/misc/SessionStartup.h"

#include <cstdint>

#include "boost/shared_ptr.h"
#include "gpg/core/time/Timer.h"
#include "lua/LuaTableIterator.h"
#include "moho/lua/CScrLuaObjectFactory.h"
#include "moho/misc/LaunchInfoBase.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/net/CClientManagerImpl.h"
#include "moho/sim/SSTICommandSource.h"
#include "moho/sim/WldSessionInfo.h"

namespace moho
{
  namespace
  {
    constexpr std::uint8_t kInvalidSourceIndex = 255u; // as SessionStartup.cpp
  } // namespace

  /**
   * Address: 0x0088BBB0 (FUN_0088BBB0, sub_88BBB0)
   *
   * What it does:
   * Seeds one local command source, applies it to all army launch source sets,
   * and binds a one-client local client-manager lane for single-player startup.
   * `WLD_SetupSessionInfo` and the session restart (0x0088C9D0) both use it.
   */
  void SetupSinglePlayerCommandSourceLane(SWldSessionInfo& sessionInfo, const char* playerName)
  {
    auto* const launchInfo = sessionInfo.mLaunchInfo.get();
    if (launchInfo == nullptr) {
      return;
    }

    const char* const localPlayerName = playerName != nullptr ? playerName : "";

    SSTICommandSource localCommandSource{};
    localCommandSource.mIndex = 0u;
    localCommandSource.mName = localPlayerName;
    localCommandSource.mTimeouts = -1;
    AppendSSTICommandSource(launchInfo->mCommandSources.mSrcs, localCommandSource);

    sessionInfo.mSourceId = 0u;

    BVIntSet localSourceSet{};
    (void)localSourceSet.Add(0u);
    for (ArmyLaunchInfo& armySourceInfo : launchInfo->mArmyLaunchInfo) {
      armySourceInfo.mUnitSources = localSourceSet;
    }

    IClientManager* const createdManager = CLIENT_CreateClientManager(1u, nullptr, 0, true);
    if (createdManager != sessionInfo.mClientManager && sessionInfo.mClientManager != nullptr) {
      delete sessionInfo.mClientManager;
    }
    sessionInfo.mClientManager = createdManager;

    if (sessionInfo.mClientManager != nullptr) {
      sessionInfo.mClientManager->CreateLocalClient(localPlayerName, 0, 0, sessionInfo.mSourceId);
    }
  }

  bool gSessionCheats = false; // engine addition, SessionStartup.h

  /**
   * Address: 0x0088CBC0 (FUN_0088CBC0)
   * Mangled:
   * ?WLD_SetupSessionInfo@Moho@@YA?AV?$auto_ptr@USWldSessionInfo@Moho@@@std@@ABVLuaObject@LuaPlus@@@Z
   *
   * What it does:
   * Builds one single-player session bootstrap payload from Lua launch data.
   */
  fa::auto_ptr<SWldSessionInfo> WLD_SetupSessionInfo(const LuaPlus::LuaObject& launchData)
  {
    boost::shared_ptr<LaunchInfoNew> launchInfo(new LaunchInfoNew());
    launchInfo->mCommandSources.mOriginalSource = 0;
    launchInfo->mCheatsEnabled = gSessionCheats || USER_DebugFacilitiesEnabled();

    const LuaPlus::LuaObject scenarioInfo = launchData["scenarioInfo"];
    launchInfo->mScenarioInfo = SCR_ToString(scenarioInfo);
    launchInfo->mGameMods = SCR_ToString(launchData["scenarioMods"]);

    {
      LuaPlus::LuaObject teamInfo = launchData["teamInfo"];
      LuaPlus::LuaTableIterator teamIterator(&teamInfo, 1);
      while (!teamIterator.m_isDone) {
        moho::AppendArmyLaunchInfo(launchInfo->mArmyLaunchInfo, ArmyLaunchInfo{});
        launchInfo->mStrVec.push_back(SCR_ToString(teamIterator.GetValue()));
        teamIterator.Next();
      }
    }

    {
      const LuaPlus::LuaObject randomSeed = launchData["RandomSeed"];
      if (randomSeed.IsNil()) {
        launchInfo->mInitSeed = static_cast<std::int32_t>(gpg::time::GetSystemTimer().ElapsedCycles());
      } else {
        launchInfo->mInitSeed = randomSeed.GetInteger();
      }
    }

    fa::auto_ptr<SWldSessionInfo> sessionInfo(new SWldSessionInfo());
    const char* const mapName = scenarioInfo["map"].GetString();
    sessionInfo->mMapName = mapName != nullptr ? mapName : "";
    sessionInfo->mLaunchInfo = boost::static_pointer_cast<LaunchInfoBase>(launchInfo);
    sessionInfo->mIsBeingRecorded = launchData["createReplay"].GetBoolean();
    sessionInfo->mIsReplay = false;
    sessionInfo->mIsMultiplayer = false;
    sessionInfo->mClientManager = nullptr;
    sessionInfo->mSourceId = kInvalidSourceIndex;

    const char* const playerName = launchData["playerName"].GetString();
    SetupSinglePlayerCommandSourceLane(*sessionInfo, playerName);
    return sessionInfo;
  }

} // namespace moho
