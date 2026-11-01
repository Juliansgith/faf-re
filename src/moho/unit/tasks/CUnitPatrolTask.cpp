#include "moho/unit/tasks/CUnitPatrolTask.h"
#include "moho/unit/tasks/CUnitPatrolTaskTypeInfo.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <typeinfo>

#include "Wm3Box3.h"
#include "gpg/core/containers/ArchiveSerialization.h"
#include "gpg/core/containers/FastVector.h"
#include "gpg/core/containers/ReadArchive.h"
#include "gpg/core/containers/WriteArchive.h"
#include "gpg/core/reflection/Reflection.h"
#include "moho/ai/CAiAttackerImpl.h"
#include "moho/ai/CAiTarget.h"
#include "moho/ai/CAiTransportImpl.h"
#include "moho/ai/EFormationdStatusTypeInfo.h"
#include "moho/ai/IAiCommandDispatchImpl.h"
#include "moho/ai/IFormationInstance.h"
#include "moho/ai/IFormationInstanceCountedPtrReflection.h"
#include "moho/ai/IAiNavigator.h"
#include "moho/collision/CColPrimitiveBase.h"
#include "moho/console/CConCommand.h"
#include "gpg/core/utils/Logging.h"
#include "moho/math/Wm3AxisAlignedBox3FafExtras.h"
#include "moho/entity/EntityDb.h"
#include "moho/entity/Prop.h"
#include "moho/math/QuaternionMath.h"
#include "moho/math/Vector3f.h"
#include "moho/resource/blueprints/RPropBlueprint.h"
#include "moho/resource/blueprints/RUnitBlueprint.h"
#include "moho/sim/CArmyImpl.h"
#include "moho/sim/COGrid.h"
#include "moho/sim/CSimArmyEconomyInfo.h"
#include "moho/sim/EAllianceTypeInfo.h"
#include "moho/sim/SFootprint.h"
#include "moho/sim/SOCellPos.h"
#include "moho/sim/STIMap.h"
#include "moho/sim/Sim.h"
#include "moho/task/CCommandTask.h"
#include "moho/task/ETaskStatus.h"
#include "moho/unit/CUnitCommand.h"
#include "moho/unit/CUnitCommandQueue.h"
#include "moho/unit/core/Unit.h"
#include "moho/unit/core/UnitWeapon.h"
#include "moho/unit/tasks/CUnitAttackTargetTask.h"
#include "moho/unit/tasks/CUnitRepairTask.h"
#include "moho/unit/tasks/CUnitReclaimTask.h"

namespace
{
  [[nodiscard]] gpg::RType* CachedCUnitPatrolTaskType()
  {
    gpg::RType* type = moho::CUnitPatrolTask::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::CUnitPatrolTask));
      moho::CUnitPatrolTask::sType = type;
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedCCommandTaskType()
  {
    gpg::RType* type = moho::CCommandTask::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::CCommandTask));
      moho::CCommandTask::sType = type;
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedSNavGoalType()
  {
    gpg::RType* type = moho::SNavGoal::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::SNavGoal));
      moho::SNavGoal::sType = type;
    }
    return type;
  }

  [[nodiscard]] gpg::RType* CachedBox3fType()
  {
    // Box3f has no class-static `sType`; mirror the binary's dedicated
    // `Wm3__Box3f__RType` global with a file-static lazily-resolved cache.
    static gpg::RType* cached = nullptr;
    if (!cached) {
      cached = gpg::LookupRType(typeid(Wm3::Box3<float>));
    }
    return cached;
  }

  [[nodiscard]] gpg::RType* CachedEntitySetTemplateEntityType()
  {
    gpg::RType* type = moho::EntitySetTemplate<moho::Entity>::sType;
    if (!type) {
      type = gpg::LookupRType(typeid(moho::EntitySetTemplate<moho::Entity>));
      moho::EntitySetTemplate<moho::Entity>::sType = type;
    }
    return type;
  }

  template <class TObject>
  [[nodiscard]] gpg::RRef MakeDerivedRef(TObject* const object, gpg::RType* const baseType)
  {
    gpg::RRef out{};
    out.mObj = nullptr;
    out.mType = baseType;
    if (!object) {
      return out;
    }

    gpg::RType* dynamicType = baseType;
    try {
      dynamicType = gpg::LookupRType(typeid(*object));
    } catch (...) {
      dynamicType = baseType;
    }

    std::int32_t baseOffset = 0;
    const bool isDerived = dynamicType != nullptr && baseType != nullptr && dynamicType->IsDerivedFrom(baseType, &baseOffset);
    if (!isDerived) {
      out.mObj = object;
      out.mType = dynamicType;
      return out;
    }

    out.mObj = reinterpret_cast<void*>(reinterpret_cast<char*>(object) - baseOffset);
    out.mType = dynamicType;
    return out;
  }

  /**
   * Shared 90-degree yaw quaternion used by the patrol search-box builder.
   *
   * The binary reads this rotation from the `flt_10B1A44` global (populated once
   * at static-init by FUN_00BD11C0 as `{w=cos(45deg), x=sin(45deg)*0,
   * y=sin(45deg), z=0}` -> a pure +Y rotation of 90 degrees). Modeled here as the
   * equivalent named constant so the recovered behavior uses a typed quaternion
   * instead of a raw data-address dereference.
   */
  const Wm3::Quaternionf kSearchBoxYawQuat{0.70710677f, 0.0f, 0.70710677f, 0.0f};

  /**
   * Length threshold (`dword_DFF0AC` = 0.001f) below which the patrol leg
   * direction is treated as degenerate and replaced with the `+Z` fallback.
   */
  constexpr float kPatrolLegMinLength = 0.001f;

  /**
   * Resolves the queue-head (`front == true`) or queue-tail command from the
   * owner unit's command queue, returning null when the queue is empty or the
   * weak slot is expired. Mirrors the binary's `_Myfirst`/`_Mylast` guard plus
   * weak-owner decode (the `count>>3` empty-check and the `-4` owner adjustment
   * that `WeakPtr::GetObjectPtr` performs).
   */
  [[nodiscard]] moho::CUnitCommand* ResolvePatrolQueueCommand(
    moho::CUnitCommandQueue* const commandQueue,
    const bool front
  ) noexcept
  {
    if (commandQueue == nullptr || commandQueue->mCommandVec.empty()) {
      return nullptr;
    }

    const moho::WeakPtr<moho::CUnitCommand>& slot =
      front ? commandQueue->mCommandVec.front() : commandQueue->mCommandVec.back();
    return slot.GetObjectPtr();
  }
} // namespace

namespace
{
  /**
   * Engine addition (performance, `sim_PatrolPropCache`). The patrol reclaim sweep searched its leg's
   * oriented box for units AND props every tick -- in an AI game a quarter of the sim thread, since the box
   * covers a whole patrol leg and a map holds ~12k props. Props rarely move and the box only changes with
   * the leg, so each task keeps the props its box held when the box was set (weak pointers: a destroyed
   * prop drops out by itself) and only searches the box for units every tick. Props created or moved since
   * (falling and sinking trees, mostly) come from a log `PROP_NoteChanged` keeps, one live entry per prop,
   * and are re-tested against the box. Same candidates as the binary's search; a tie in weighted distance,
   * which the binary breaks by grid order, redoes the tick from that search (`EvaluatePatrolReclaimAttack`).
   */
  bool sim_PatrolPropCacheCheck = false;
  moho::TConVar<bool> gTConVar_sim_PatrolPropCacheCheck(
    "sim_PatrolPropCacheCheck", "Compare sim_PatrolPropCache's candidates with the full search and log differences (slow).",
    &sim_PatrolPropCacheCheck
  );
  bool sim_PatrolPropCache = true;
  moho::TConVar<bool> gTConVar_sim_PatrolPropCache(
    "sim_PatrolPropCache",
    "Patrol reclaim sweeps keep the props of their search box and only search it for units every tick.",
    &sim_PatrolPropCache
  );

  struct SPropLogEntry
  {
    std::uint64_t mSeq;
    moho::WeakPtr<moho::Entity> mProp;   // empty once the prop has a newer entry
    Wm3::AxisAlignedBox3f mPriorBounds; // covers the prop's bounds before every change since its previous entry
  };
  std::deque<SPropLogEntry> sPropLog; // oldest first; deque elements stay put, which the weak links need
  std::unordered_map<const moho::Entity*, std::uint64_t> sPropLogLatest; // prop -> seq of its live entry
  std::uint64_t sPropLogNext = 1u;
  constexpr std::size_t kPropLogMax = 1u << 15;

  struct SCachedProp
  {
    moho::WeakPtr<moho::Entity> mProp;
    const moho::Entity* mKey; // the address `mMembers` holds, kept for when `mProp` has gone empty
  };

  struct SPatrolPropCache
  {
    Wm3::Box3f mBox{};
    std::uint64_t mSeenSeq = 0u; // 0: never built
    std::vector<SCachedProp> mProps;
    std::unordered_set<const moho::Entity*> mMembers; // the live props of `mProps`
  };
  std::unordered_map<const moho::CUnitPatrolTask*, SPatrolPropCache> sPatrolCaches;

  [[nodiscard]] bool BoundsOverlap(const Wm3::AxisAlignedBox3f& a, const Wm3::AxisAlignedBox3f& b) noexcept
  {
    return !(a.Min.x > b.Max.x) && !(b.Min.x > a.Max.x) && !(a.Min.y > b.Max.y) && !(b.Min.y > a.Max.y) &&
           !(a.Min.z > b.Max.z) && !(b.Min.z > a.Max.z);
  }

  [[nodiscard]] Wm3::AxisAlignedBox3f BoundsUnion(const Wm3::AxisAlignedBox3f& a, const Wm3::AxisAlignedBox3f& b) noexcept
  {
    Wm3::AxisAlignedBox3f out = a;
    out.Min.x = std::min(a.Min.x, b.Min.x);
    out.Min.y = std::min(a.Min.y, b.Min.y);
    out.Min.z = std::min(a.Min.z, b.Min.z);
    out.Max.x = std::max(a.Max.x, b.Max.x);
    out.Max.y = std::max(a.Max.y, b.Max.y);
    out.Max.z = std::max(a.Max.z, b.Max.z);
    return out;
  }

  void RebuildPatrolPropCache(SPatrolPropCache& cache, moho::COGrid& grid, const Wm3::Box3f& box)
  {
    using namespace moho;
    gpg::core::FastVectorN<CollisionResult, 10> hits;
    grid.CollectEntitiesInBoxEntities(hits, ENTITYTYPE_Prop, box);
    cache.mProps.clear();
    cache.mMembers.clear();
    for (const CollisionResult& hit : hits) {
      if (cache.mMembers.insert(hit.sourceEntity).second) {
        cache.mProps.push_back(SCachedProp{WeakPtr<Entity>(hit.sourceEntity), hit.sourceEntity});
      }
    }
    cache.mBox = box;
    cache.mSeenSeq = sPropLogNext;
  }

  /// Brings the cache up to the props created or moved since it last looked.
  void CatchUpPatrolPropCache(SPatrolPropCache& cache, const Wm3::Box3f& box, const Wm3::AxisAlignedBox3f& queryBounds)
  {
    using namespace moho;
    // Destroyed props first, so no address in `mMembers` can belong to a prop allocated since.
    std::erase_if(cache.mProps, [&cache](const SCachedProp& cached) {
      if (cached.mProp.GetObjectPtr() != nullptr) {
        return false;
      }
      cache.mMembers.erase(cached.mKey);
      return true;
    });
    const std::uint64_t logFirst = sPropLog.front().mSeq;
    for (std::size_t i = static_cast<std::size_t>(cache.mSeenSeq - logFirst); i < sPropLog.size(); ++i) {
      const SPropLogEntry& entry = sPropLog[i];
      Entity* const prop = entry.mProp.GetObjectPtr();
      if (prop == nullptr) {
        continue; // superseded by a later entry, or destroyed
      }
      const bool nearNow = prop->CollisionExtents != nullptr && BoundsOverlap(queryBounds, prop->mAABox);
      if (!nearNow && !BoundsOverlap(queryBounds, entry.mPriorBounds)) {
        continue; // outside the box before and after: never cached here
      }
      bool inside = false;
      if (nearNow) {
        CollisionResult collision{};
        inside = prop->CollisionExtents->CollideBox(&box, &collision);
      }
      if (inside) {
        if (cache.mMembers.insert(prop).second) {
          cache.mProps.push_back(SCachedProp{WeakPtr<Entity>(prop), prop});
        }
      } else if (cache.mMembers.erase(prop) != 0u) {
        std::erase_if(cache.mProps, [prop](const SCachedProp& cached) { return cached.mKey == prop; });
      }
    }
    cache.mSeenSeq = sPropLogNext;
  }

  /// The sweep's candidates for `box`: the cached props (none when `withProps` is false), then the units
  /// the box holds this tick.
  void GatherPatrolCandidates(
    const moho::CUnitPatrolTask* const task,
    moho::COGrid& grid,
    const Wm3::Box3f& box,
    const bool withProps,
    std::vector<moho::Entity*>& out
  )
  {
    using namespace moho;
    out.clear();
    if (withProps) {
      const Wm3::AxisAlignedBox3f queryBounds = AxisAlignedBoundsFromBox(box);
      SPatrolPropCache& cache = sPatrolCaches[task];
      const std::uint64_t logFirst = sPropLog.empty() ? sPropLogNext : sPropLog.front().mSeq;
      if (cache.mSeenSeq == 0u || cache.mSeenSeq < logFirst || std::memcmp(&cache.mBox, &box, sizeof(box)) != 0) {
        RebuildPatrolPropCache(cache, grid, box);
      } else if (cache.mSeenSeq < sPropLogNext) {
        CatchUpPatrolPropCache(cache, box, queryBounds);
      }
      for (const SCachedProp& cached : cache.mProps) {
        Entity* const prop = cached.mProp.GetObjectPtr();
        if (prop != nullptr && prop->CollisionExtents != nullptr && BoundsOverlap(queryBounds, prop->mAABox)) {
          out.push_back(prop);
        }
      }
    }
    gpg::core::FastVectorN<CollisionResult, 10> unitHits;
    grid.CollectEntitiesInBoxEntities(unitHits, ENTITYTYPE_Unit, box);
    for (const CollisionResult& hit : unitHits) {
      out.push_back(hit.sourceEntity);
    }
  }
} // namespace

void moho::PROP_NoteChanged(Entity* const prop, const Wm3::AxisAlignedBox3f& priorBounds)
{
  if (prop == nullptr || !sim_PatrolPropCache) {
    return;
  }
  // One live entry per prop: an earlier one goes empty, and its prior bounds carry over, since a cache that
  // has not read it yet decided on the prop with those bounds.
  Wm3::AxisAlignedBox3f prior = priorBounds;
  const auto latest = sPropLogLatest.find(prop);
  if (latest != sPropLogLatest.end() && !sPropLog.empty() && latest->second >= sPropLog.front().mSeq) {
    SPropLogEntry& previous = sPropLog[static_cast<std::size_t>(latest->second - sPropLog.front().mSeq)];
    if (previous.mProp.GetObjectPtr() == prop) {
      prior = BoundsUnion(prior, previous.mPriorBounds);
      previous.mProp = WeakPtr<Entity>();
    }
  }
  sPropLogLatest[prop] = sPropLogNext;
  sPropLog.push_back(SPropLogEntry{sPropLogNext++, WeakPtr<Entity>(prop), prior});
  while (sPropLog.size() > kPropLogMax) {
    sPropLog.pop_front(); // a cache older than the log's front rebuilds
  }
}


namespace moho
{
  gpg::RType* CUnitPatrolTask::sType = nullptr;

  /**
   * Address: 0x0061B0B0 (FUN_0061B0B0, Moho::CUnitPatrolTask::CUnitPatrolTask default ctor)
   *
   * What it does:
   * Default-constructs one patrol-task lane. The embedded `Listener<...>` and
   * `EntitySetTemplate<Entity>` members self-link their intrusive nodes (the
   * binary's `a1+56/60`, `a1+72/76`, `a1+200/204` self-link stores), and the
   * scalar goal / flag / search-box / tick lanes are zero-cleared. Used by the
   * SerConstruct reflection callback (FUN_0061AD10) which then deserializes
   * payload fields via the archive `Read` path.
   */
  CUnitPatrolTask::CUnitPatrolTask() noexcept
    : CCommandTask()
    , Listener<ECommandEvent>()
    , Listener<EFormationdStatus>()
    , mDispatch(nullptr)
    , mBoundCommand(nullptr)
    , mFormationInstance(nullptr)
    , mGoal{}
    , mMoving(false)
    , mNavStalled(false)
    , mInFormation(false)
    , mPad83(0)
    , mSearchBox{}
    , mTickCounter(0)
    , mPadC4{}
    , mMembership()
  {
  }

  /**
   * Address: 0x0061AE50 (FUN_0061AE50, Moho::CUnitPatrolTask::CUnitPatrolTask)
   * Mangled: ??0CUnitPatrolTask@Moho@@QAE@PAVCCommandTask@1@PBXPAVIFormationInstance@1@_N@Z
   *
   * IDA signature:
   * Moho::CUnitPatrolTask* __thiscall CUnitPatrolTask(
   *   CUnitPatrolTask* this, CCommandTask* dispatchTask, const void* goalPayload,
   *   IFormationInstance* formationInstance, bool inFormation);
   *
   * What it does:
   * Constructs one active patrol task. Runs the `CCommandTask` base ctor,
   * publishes the task/listener identity, self-links the two embedded listener
   * nodes and the per-army `EntitySetTemplate<Entity>` membership node, copies
   * the navigator goal payload, sets the formation-mode flag, resolves the queue
   * head command, splices the command-event listener into that command's
   * broadcaster ring, links the membership node into `mUnit->mSim->mEntityDB`'s
   * registered-set list, splices the formation-status listener into the bound
   * formation instance's broadcaster ring, refreshes navigator speed-through
   * state, issues one desired-target clear on immobile need-unpack attackers,
   * builds the oriented `mSearchBox`, and enters the Preparing task state.
   */
  CUnitPatrolTask::CUnitPatrolTask(
    CCommandTask* const dispatchTask,
    const void* const goalPayload,
    IFormationInstance* const formationInstance,
    const bool inFormation
  )
    : CCommandTask(dispatchTask)
    , Listener<ECommandEvent>()
    , Listener<EFormationdStatus>()
    , mDispatch(static_cast<IAiCommandDispatchImpl*>(dispatchTask))
    , mBoundCommand(nullptr)
    , mFormationInstance(formationInstance)
    , mGoal{}
    , mMoving(false)
    , mNavStalled(false)
    , mInFormation(inFormation)
    , mPad83(0)
    , mSearchBox{}
    , mTickCounter(0)
    , mPadC4{}
    , mMembership()
  {
    // Copy the navigator goal payload (const void* -> SNavGoal), matching the
    // binary's `rep movsd` of 9 dwords (0x24 bytes) from the goal argument.
    if (goalPayload != nullptr) {
      mGoal = *static_cast<const SNavGoal*>(goalPayload);
    }

    // Publish the task identity + set the owner-unit patrol state bit and enter
    // the linking sequence. The base ctor already resolved `mUnit` from dispatch.
    Unit* const unit = mUnit;

    // Owner unit gains the patrol move-state bit (0x1000). The binary loads and
    // writes back the high dword unchanged, so the 64-bit mask stays intact.
    unit->mUnitVarDat.mUnitStates |= static_cast<std::uint64_t>(0x1000u);

    // Resolve the queue-head command and store it as the bound patrol command
    // (binary `this + 0x54`, the command the search-box builder later reads as
    // the leg-start).
    CUnitCommandQueue* const commandQueue = unit->CommandQueue;
    CUnitCommand* const frontCommand = ResolvePatrolQueueCommand(commandQueue, /*front=*/true);
    mBoundCommand = frontCommand;

    // Subscribe to the front command's events. The command is not
    // null-checked: 0x0061AF57 adds 0x34 to whatever the queue returned.
    frontCommand->AddListener(this);

    // Link the per-army membership node into the sim entity DB's registered-set
    // list (binary `mUnit->mSim->mEntityDB + 0x18`).
    EntityDB* const entityDb = unit->SimulationRef->mEntityDB;
    mMembership.ListLinkBefore(&entityDb->mRegisteredEntitySets);

    // When a formation instance is bound, splice the formation-status listener
    // into that instance's broadcaster ring.
    // The ring is `IFormationInstance`'s `Broadcaster` base at +0x08
    // (0x0061AFC4); the tree used to link at +0x04, over the instance's
    // `CountedObject` reference count.
    if (mFormationInstance != nullptr) {
      mFormationInstance->AddListener(this);
    }

    // Refresh navigator speed-through state when the unit still has a navigator.
    if (unit->AiNavigator != nullptr) {
      unit->UpdateSpeedThroughStatus();
    }

    // Immobile need-unpack attackers get their desired target cleared so the
    // pack/unpack transition is not held by a stale attack target.
    if (unit->IsUnitState(UNITSTATE_Immobile)) {
      if (const RUnitBlueprint* const blueprint = unit->GetBlueprint();
          blueprint != nullptr && blueprint->AI.NeedUnpack) {
        if (CAiAttackerImpl* const attacker = unit->AiAttacker; attacker != nullptr) {
          CAiTarget clearTarget{};
          clearTarget.targetType = EAiTargetType::AITARGET_None;
          clearTarget.position = Wm3::Vector3f{0.0f, 0.0f, 0.0f};
          clearTarget.targetPoint = -1;
          clearTarget.targetIsMobile = false;
          attacker->SetDesiredTarget(&clearTarget);
        }
      }
    }

    // Build the oriented search box, then enter the Preparing state.
    RecomputePatrolSearchBox();
    mTaskState = TASKSTATE_Preparing;
  }

  /**
   * Address: 0x0061B140 (FUN_0061B140, Moho::CUnitPatrolTask::~CUnitPatrolTask)
   * Address: 0x0061B090 (FUN_0061B090, vtable-slot-2 scalar deleting
   * destructor: tail-calls the body below then conditionally frees the
   * object -- ordinary C++ `delete` semantics, not modeled as a separate
   * function here)
   * Mangled: ??1CUnitPatrolTask@Moho@@QAE@@Z
   *
   * IDA signature:
   * void __thiscall Moho::CUnitPatrolTask::~CUnitPatrolTask(Moho::CUnitPatrolTask *this);
   *
   * What it does:
   * Complete-object destructor. Detaches this patrol task from the owner unit's
   * listener/navigator state and clears the transient patrol move state before
   * base `CCommandTask` teardown. See the header block for the full binary-order
   * teardown sequence.
   *
   * The MSVC per-sub-object vtable restores and the second, unconditional
   * listener relinks at the binary head/tail (0x0061B15E, 0x0061B28E..0x0061B2CC)
   * are dead stores to the dying object's own memory; together with the embedded
   * `EntitySetTemplate<Entity>` membership teardown (0x0061B258..0x0061B28C, i.e.
   * `~EntitySetTemplate<Entity>()` on `mMembership`) they are handled implicitly
   * by the C++ destructor and are not reproduced as raw pointer arithmetic.
   */
  CUnitPatrolTask::~CUnitPatrolTask()
  {
    sPatrolCaches.erase(this); // engine addition, sim_PatrolPropCache
    // (1) Unconditionally unlink the command-event listener lane
    // (0x0061B17A: splice prev/next, then self-relink the node).
    Listener<ECommandEvent>::ListUnlink();

    // (2) Navigator branch: stop honoring the formation, and abort the active
    // move when the unit's current and previous positions differ
    // (0x0061B1BD: `Vector3<float>::CompareArrays`, an exact memcmp; a non-zero
    // result falls through to `AbortMove`, zero jumps past it at 0x0061B1C4).
    if (IAiNavigator* const navigator = mUnit->AiNavigator; navigator != nullptr) {
      navigator->IgnoreFormation(false);
      if (mUnit->mVarDat.mCurTransform.pos_ != mUnit->mVarDat.mLastTransform.pos_) {
        navigator->AbortMove();
      }
    }

    // (3) Formation-status listener lane: unlink only when a formation instance
    // is bound (0x0061B1CF: `cmp [esi+58h], 0`).
    if (mFormationInstance != nullptr) {
      Listener<EFormationdStatus>::ListUnlink();
    }

    // (4) Clear the two patrol move-state bits on the owner unit
    // (0x0061B1ED / 0x0061B206: `&= ~0x2000000` then `&= ~0x1000`; the binary's
    // read-back of the high dword is a no-op, so the 64-bit masks stay intact).
    mUnit->mUnitVarDat.mUnitStates &= ~static_cast<std::uint64_t>(0x2000000u);
    mUnit->mUnitVarDat.mUnitStates &= ~static_cast<std::uint64_t>(0x1000u);

    // (5) Reset the owner unit's guarded position to the zero vector
    // (0x0061B21F..0x0061B250: three `movss` stores from the shared `vec0`
    // zero-vector global into `Unit::GuardedPos`).
    mUnit->GuardedPos = Wm3::Vector3f::Zero();
  }

  /**
   * Address: 0x0061C3E0 (FUN_0061C3E0, Moho::CUnitPatrolTask::OnEvent)
   *
   * What it does:
   * See the header declaration for the full behavior summary.
   */
  void CUnitPatrolTask::OnEvent(const ECommandEvent /*event*/)
  {
    SOCellPos cell{};
    CUnitCommand::GetPosition(mBoundCommand, mUnit, &cell);

    mGoal.minX = cell.x;
    mGoal.minZ = cell.z;
    mGoal.maxX = cell.x + 1;
    mGoal.maxZ = cell.z + 1;
    mGoal.aux0 = 0;
    mGoal.aux1 = 0;
    mGoal.aux2 = 0;
    mGoal.aux3 = 0;
    mGoal.aux4 = 0;

    if (mUnit->AiNavigator != nullptr && mMoving) {
      mUnit->AiNavigator->SetGoal(mGoal);
    }

    RecomputePatrolSearchBox();
  }

  /**
   * Address: 0x0061C470 (FUN_0061C470, Moho::CUnitPatrolTask::OnEvent)
   *
   * What it does:
   * See the header declaration for the full behavior summary.
   */
  void CUnitPatrolTask::OnEvent(const EFormationdStatus event)
  {
    if (event == FORMATIONSTATUS_FormationAtGoal) {
      mNavStalled = true;
    }
  }

  namespace
  {
    /**
     * Address: 0x0061B610 (FUN_0061B610)
     *
     * IDA signature:
     * bool __usercall sub_61B610@<al>(CUnitPatrolTask* this@<ebx>);
     *
     * What it does:
     * Patrol-arrival test: returns true when the (optionally speed-expanded)
     * goal rectangle still covers the owner unit's footprint-aligned grid
     * origin. When a move is in flight (`mMoving`), the goal rectangle is
     * inflated by ceil(mInfoCache.mFormationTopSpeed) cells before the
     * containment test.
     */
    [[nodiscard]] bool PatrolGoalCoversUnitOrigin(const CUnitPatrolTask& task)
    {
      Unit* const unit = task.mUnit;

      std::int32_t minX = task.mGoal.minX;
      std::int32_t minZ = task.mGoal.minZ;
      std::int32_t maxX = task.mGoal.maxX;
      std::int32_t maxZ = task.mGoal.maxZ;

      if (task.mMoving) {
        const float topSpeed = unit->mInfoCache.mFormationTopSpeed;
        const auto expand = static_cast<std::int32_t>(std::ceil(topSpeed));
        minX -= expand;
        minZ -= expand;
        maxX += expand;
        maxZ += expand;
      }

      const SFootprint& footprint = unit->GetFootprint();
      const Wm3::Vec3f& position = unit->GetPosition();
      const auto originX = static_cast<std::int16_t>(
        static_cast<int>(std::lrintf(position.x - static_cast<float>(footprint.mSizeX) * 0.5f))
      );
      const auto originZ = static_cast<std::int16_t>(
        static_cast<int>(std::lrintf(position.z - static_cast<float>(footprint.mSizeZ) * 0.5f))
      );

      return minX <= originX && maxX >= originX && minZ <= originZ && maxZ >= originZ;
    }
  } // namespace

  /**
   * Address: 0x0061B710 (FUN_0061B710, Moho::CUnitPatrolTask::FindTarget)
   *
   * IDA signature:
   * Moho::Entity* __stdcall FindTarget(Moho::CUnitPatrolTask* this);
   *
   * What it does:
   * Builds a scratch weak-ref collector from the owner unit's in-range blips
   * whose collision shape overlaps the patrol search box, asks the owner
   * attacker for the best enemy (primary weapon, guard-scan radius), and
   * returns that enemy only when it lies within guard-scan radius. The
   * collector is torn down (intrusive weak links unspliced) on all exits.
   */
  Entity* CUnitPatrolTask::FindTarget()
  {
    Unit* const unit = mUnit;
    if (unit->AiAttacker == nullptr) {
      return nullptr;
    }

    // Scratch collector of blips overlapping the patrol search box. Its
    // destructor performs the intrusive weak-ref range-unlink teardown that the
    // binary open-codes (sub_61CA70 + operator delete[]).
    gpg::core::FastVectorN<WeakPtr<Entity>, 20> overlappingBlips;

    const gpg::core::FastVectorN<WeakPtr<Entity>, 20>& blips = unit->mBlipsInRange;
    const std::ptrdiff_t blipCount = blips.end() - blips.begin();
    for (std::ptrdiff_t i = 0; i < blipCount; ++i) {
      Entity* const blip = unit->mBlipsInRange.begin()[i].GetObjectPtr();
      if (blip == nullptr || blip->mVarDat.mIsDead || blip->DestroyQueuedFlag) {
        continue;
      }

      CColPrimitiveBase* const collisionShape = blip->CollisionExtents;
      if (collisionShape == nullptr) {
        continue;
      }

      CollisionResult collision{};
      if (collisionShape->CollideBox(&mSearchBox, &collision)) {
        collision.sourceEntity = blip;
        // A temporary weak pointer to the blip, copied into the collector
        // (sub_61C5E0) and unlinked again as it dies (sub_5A6DE0).
        overlappingBlips.push_back(WeakPtr<Entity>(blip));
      }
    }

    CAiAttackerImpl* const attacker = unit->AiAttacker;
    UnitWeapon* const primaryWeapon = attacker->GetPrimaryWeapon();
    const float guardScanRadius = unit->GetBlueprint()->AI.GuardScanRadius;
    Entity* const bestEnemy = attacker->FindBestEnemy(primaryWeapon, &overlappingBlips, guardScanRadius, 0);
    if (bestEnemy != nullptr) {
      const Wm3::Vec3f& selfPos = unit->GetPosition();
      const float dz = bestEnemy->mVarDat.mCurTransform.pos_.z - selfPos.z;
      const float dx = bestEnemy->mVarDat.mCurTransform.pos_.x - selfPos.x;
      const float distance = std::sqrt(dz * dz + dx * dx);
      if (distance > unit->GetBlueprint()->AI.GuardScanRadius) {
        return nullptr;
      }
    }

    return bestEnemy;
  }

  /**
   * Address: 0x0061B9C0 (FUN_0061B9C0)
   *
   * IDA signature:
   * Moho::Entity* __stdcall sub_61B9C0(CUnitPatrolTask* this);
   *
   * What it does:
   * Scores COGrid entities inside the patrol search box for reclaim (props) or
   * opportunistic attack (units), honoring the PATROLHELPER precondition, the
   * COMMAND/SACU_BEHAVIOR formation opt-out, the army no-rush zone, alliance,
   * reclaimable category, map bounds, guard-return radius, and a per-candidate
   * distance weight. Returns the nearest weighted candidate, or null.
   *
   * Note: the army no-rush-zone extents used for the reclaim (v40) / attack
   * (v39) mass/energy gate are read from the army's no-rush lane fields
   * (`StartPosition`/`NoRushOffset*`/`NoRushTicks`/`NoRushRadius`) exactly as
   * the binary computes them via the CArmyImpl no-rush accessor; the half-range
   * abs-value math mirrors the `fild`/`fchs`/`*0.5` sequence at 0x0061BB60.
   */
  Entity* CUnitPatrolTask::EvaluatePatrolReclaimAttack()
  {
    Unit* const unit = mUnit;

    // Only patrol-helper-capable units run the reclaim/attack sweep.
    if (!unit->IsInCategory("PATROLHELPER")) {
      return nullptr;
    }

    // Formation members that are commander/SACU units opt out of the sweep.
    if (mInFormation) {
      if (unit->IsInCategory("COMMAND") || unit->IsInCategory("SACU_BEHAVIOR")) {
        return nullptr;
      }
    }

    CArmyImpl* const army = unit->ArmyRef;

    // Storage-pressure gates, read out of the army economy (`IArmy` vtable slot
    // +0x24 = `GetEconomy`, called twice at 0x0061BB35 and 0x0061BB5B). The
    // binary loads `mStored` at economy+0x18/+0x1C and `mMaxStorage` at
    // economy+0x40/+0x48, converts each u64 max to floating point with the
    // two-`fild` unsigned-64 sequence at 0x0061BB78..0x0061BB9B, scales it by
    // `dword_E4F7C8` = 0.75f and compares (0x0061BBEA, 0x0061BBFB).
    //
    // So: "am I already sitting on more than three quarters of a full store of
    // this resource". A prop is worth reclaiming only for a resource that is
    // *not* near-capped, and an ally is worth assisting only when both stores
    // are, i.e. when there is surplus to spend.
    const CSimArmyEconomyInfo* const economy = army->GetEconomy();
    const SEconTotals& totals = economy->economy;
    const bool energyStorageNearFull =
      totals.mStored.ENERGY > static_cast<float>(totals.mMaxStorage.ENERGY) * 0.75f;
    const bool massStorageNearFull =
      totals.mStored.MASS > static_cast<float>(totals.mMaxStorage.MASS) * 0.75f;

    // No-rush zone centre: start position offset by the per-army no-rush offset.
    // X pairs with the candidate's X and Z with its Z -- 0x0061C00A subtracts
    // `position.z` (entity+0xB4) from `StartPosition.Y + NoRushOffsetY` and
    // 0x0061C018 subtracts `position.x` (entity+0xAC) from
    // `StartPosition.X + NoRushOffsetX`.
    const float noRushCenterX = army->mVarDat.mArmyStart.X() + army->mVarDat.mNoRushOffset.x;
    const float noRushCenterZ = army->mVarDat.mArmyStart.Y() + army->mVarDat.mNoRushOffset.y;

    gpg::core::FastVectorN<CollisionResult, 10> collisions;
    Sim* const sim = unit->SimulationRef;
    // sim_PatrolPropCache: the cached props of the box plus this tick's units (GatherPatrolCandidates);
    // otherwise the binary's one search for both. Only `sourceEntity` is read below.
    static std::vector<Entity*> sCandidates;
    // Props pass the prop branch only for a unit that can reclaim, so a sweep for one that cannot
    // looks for units alone (the binary tests this per candidate).
    const bool canReclaim = unit->IsInCategory("RECLAIM");
    if (sim_PatrolPropCache) {
      GatherPatrolCandidates(this, *sim->mOGrid, mSearchBox, canReclaim, sCandidates);
      if (sim_PatrolPropCacheCheck) {
        // compare with the binary's search, as sets
        gpg::core::FastVectorN<CollisionResult, 10> reference;
        sim->mOGrid->CollectEntitiesInBox(reference, static_cast<EEntityType>(ENTITYTYPE_Unit | ENTITYTYPE_Prop), mSearchBox);
        std::vector<Entity*> want;
        for (const CollisionResult& hit : reference) { want.push_back(hit.sourceEntity); }
        std::vector<Entity*> have(sCandidates);
        std::sort(want.begin(), want.end());
        std::sort(have.begin(), have.end());
        if (want != have) {
          static int sReports = 0;
          if (sReports++ < 40) {
            std::vector<Entity*> missing, extra;
            std::set_difference(want.begin(), want.end(), have.begin(), have.end(), std::back_inserter(missing));
            std::set_difference(have.begin(), have.end(), want.begin(), want.end(), std::back_inserter(extra));
            gpg::Logf("PATROLCHECK tick %u task %p: want %zu have %zu missing %zu extra %zu", static_cast<unsigned>(sim->mCurTick),
                      static_cast<const void*>(this), want.size(), have.size(), missing.size(), extra.size());
            for (Entity* e : missing) {
              gpg::Logf("  missing id 0x%08x unit=%d prop=%d pos %.2f %.2f %.2f last %.2f %.2f %.2f", static_cast<unsigned>(e->id_),
                        e->IsUnit() != nullptr, e->IsProp() != nullptr, e->mVarDat.mCurTransform.pos_.x, e->mVarDat.mCurTransform.pos_.y,
                        e->mVarDat.mCurTransform.pos_.z, e->mVarDat.mLastTransform.pos_.x, e->mVarDat.mLastTransform.pos_.y,
                        e->mVarDat.mLastTransform.pos_.z);
            }
            for (Entity* e : extra) {
              gpg::Logf("  extra id 0x%08x unit=%d prop=%d pos %.2f %.2f %.2f", static_cast<unsigned>(e->id_), e->IsUnit() != nullptr,
                        e->IsProp() != nullptr, e->mVarDat.mCurTransform.pos_.x, e->mVarDat.mCurTransform.pos_.y, e->mVarDat.mCurTransform.pos_.z);
            }
          }
        }
      }
    } else {
      sim->mOGrid->CollectEntitiesInBoxEntities(
        collisions,
        static_cast<EEntityType>(ENTITYTYPE_Unit | ENTITYTYPE_Prop),
        mSearchBox
      );
      sCandidates.clear();
      for (const CollisionResult& hit : collisions) {
        sCandidates.push_back(hit.sourceEntity);
      }
    }

    bool fromCache = sim_PatrolPropCache;
  rescan:
    Entity* bestCandidate = nullptr;
    float bestWeightedDistanceSq = gpg::pInf;
    bool bestTied = false;

    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(sCandidates.size());
    for (std::ptrdiff_t i = 0; i < count; ++i) {
      Entity* const candidate = sCandidates[static_cast<std::size_t>(i)];
      {
        // Engine addition (performance): no weight below is under 0.5 (an enemy's) and scaling by a power
        // of two is exact, so a candidate whose distance already exceeds the best so far at that weight can
        // neither beat nor tie it. Every test below is free of side effects, so it is skipped before them.
        const Wm3::Vec3f& selfPos = unit->GetPosition();
        const float dx = selfPos.x - candidate->mVarDat.mCurTransform.pos_.x;
        const float dy = selfPos.y - candidate->mVarDat.mCurTransform.pos_.y;
        const float dz = selfPos.z - candidate->mVarDat.mCurTransform.pos_.z;
        if ((dx * dx + dy * dy + dz * dz) * 0.5f > bestWeightedDistanceSq) {
          continue;
        }
      }
      // Both type tests run for every candidate, in this order: Entity vtable
      // +0x10 `IsUnit` at 0x0061BCF1, +0x14 `IsProp` at 0x0061BD05.
      Unit* const candidateUnit = candidate->IsUnit();
      Prop* const candidateProp = candidate->IsProp();
      float weight = 1.0f;

      // 0x0061BD21 dispatches Unit vtable +0x28, which is `IUnit::IsDead` --
      // slot 10 of the IUnit vtable, two slots below `IsBeingBuilt` at +0x34.
      // Reading it as `IsBeingBuilt` sent every unit under construction down
      // the prop-reclaim branch, where the only gates are "I can reclaim" and
      // "it is RECLAIMABLE". An allied unit on the factory pad passes both, and
      // `Execute` then turns an allied candidate into a `CUnitRepairTask` -- so
      // an engineer rolling off a factory with an attack-move rally point
      // immediately turned around and assisted the factory instead of going out
      // to reclaim. With the real test the same unit falls through to the unit
      // branch below and is dropped by the stationary check, which is what
      // leaves the engineer free to reclaim.
      if (candidateUnit == nullptr || candidateUnit->IsDead()) {
        // Prop reclaim branch. A candidate that is neither a live unit nor a
        // prop is skipped outright (0x0061BE40 `test edi, edi`).
        if (candidateProp == nullptr) {
          continue;
        }
        if (!canReclaim) {
          continue;
        }
        if (!candidateProp->IsInCategory("RECLAIMABLE")) {
          continue;
        }
        if (energyStorageNearFull || massStorageNearFull) {
          const auto* const propBlueprint =
            reinterpret_cast<const RPropBlueprint*>(candidateProp->BluePrint);
          bool hasReclaimValue = false;
          if (!energyStorageNearFull) {
            hasReclaimValue = propBlueprint->Economy.ReclaimEnergyMax > 0.0f;
          }
          if ((massStorageNearFull || propBlueprint->Economy.ReclaimMassMax <= 0.0f) && !hasReclaimValue) {
            continue;
          }
        }
        weight = 1.0f;

        // The already-claimed set is consulted on the prop lane only: the two
        // unit lanes below jump straight past it to the map-bounds test
        // (0x0061BDDE and 0x0061BE3B both target 0x0061BF8E, while the prop
        // lane falls through the `sub_61C540` lookup at 0x0061BF81).
        if (mMembership.Contains(candidateProp)) {
          continue;
        }
      } else {
        // Unit attack/reclaim branch.
        if (candidateUnit == unit) {
          continue;
        }
        if (candidate->mVarDat.mLastTransform.pos_ != candidate->mVarDat.mCurTransform.pos_) {
          continue; // stationary — already at rest, ignore
        }
        if (candidate->mVarDat.mLayerMask == LAYER_Air) {
          continue;
        }

        const EAlliance alliance = unit->ArmyRef->GetAllianceWith(candidate->ArmyRef);
        if (alliance == ALLIANCE_Neutral) {
          continue;
        }
        if (alliance == ALLIANCE_Enemy) {
          if (!candidate->IsInCategory("RECLAIMABLE")) {
            continue;
          }
          weight = 0.5f;
        } else {
          // Allied damaged unit, but only while both stores are near full.
          if (!energyStorageNearFull || !massStorageNearFull
              || candidate->mVarDat.mHealth >= (candidate->mVarDat.mMaxHealth * 0.9f)
              || candidateUnit->IsUnitState(UNITSTATE_BeingReclaimed)) {
            continue;
          }
          weight = 2.0f;
        }
      }

      const bool wholeMap = army->UseWholeMap();
      if (!candidate->SimulationRef->mMapData->IsWithin(candidate->mVarDat.mCurTransform.pos_, 1.0f, wholeMap)) {
        continue;
      }

      // Guard-return radius gate around the no-rush center.
      if (army->mVarDat.mNoRushTimer > 0) {
        const float ddz = noRushCenterZ - candidate->mVarDat.mCurTransform.pos_.z;
        const float ddx = noRushCenterX - candidate->mVarDat.mCurTransform.pos_.x;
        const float noRushDistance = std::sqrt(ddz * ddz + ddx * ddx);
        if (noRushDistance > army->mVarDat.mNoRushRadius) {
          continue;
        }
      }

      // Weighted squared distance from this unit to the candidate; keep nearest.
      const Wm3::Vec3f& selfPos = unit->GetPosition();
      const float dx = selfPos.x - candidate->mVarDat.mCurTransform.pos_.x;
      const float dy = selfPos.y - candidate->mVarDat.mCurTransform.pos_.y;
      const float dz = selfPos.z - candidate->mVarDat.mCurTransform.pos_.z;
      const float weightedDistanceSq = (dx * dx + dy * dy + dz * dz) * weight;
      if (bestWeightedDistanceSq > weightedDistanceSq) {
        bestWeightedDistanceSq = weightedDistanceSq;
        bestCandidate = candidate;
        bestTied = false;
      } else if (bestCandidate != nullptr && bestWeightedDistanceSq == weightedDistanceSq) {
        bestTied = true;
      }
    }

    if (bestTied && fromCache) {
      // The binary takes the first of equally near candidates in grid order, which the cache does not
      // keep: redo this tick from the binary's search.
      sim->mOGrid->CollectEntitiesInBoxEntities(
        collisions,
        static_cast<EEntityType>(ENTITYTYPE_Unit | ENTITYTYPE_Prop),
        mSearchBox
      );
      sCandidates.clear();
      for (const CollisionResult& hit : collisions) {
        sCandidates.push_back(hit.sourceEntity);
      }
      fromCache = false;
      goto rescan;
    }
    return bestCandidate;
  }

  /**
   * Address: 0x0061B2F0 (FUN_0061B2F0, Moho::CUnitPatrolTask::RecomputePatrolSearchBox)
   *
   * IDA signature:
   * void __stdcall RecomputePatrolSearchBox(Moho::CUnitPatrolTask* this);
   *
   * What it does:
   * Rebuilds the oriented world-space `mSearchBox` for the current patrol leg.
   * See the header block for the full lane-by-lane description.
   */
  void CUnitPatrolTask::RecomputePatrolSearchBox()
  {
    Unit* const unit = mUnit;
    CUnitCommandQueue* const commandQueue = unit->CommandQueue;

    // Cache the owner's guard-scan radius, used for the box's fixed lateral
    // extent and as the base of the along-leg extent (binary `v28` = read from
    // `GetBlueprint()->AI` +0x00, i.e. `RUnitBlueprintAI::GuardScanRadius`).
    const float guardScanRadius = unit->GetBlueprint()->AI.GuardScanRadius;

    // Queue head/tail commands: only used to gate the leg-end branch below. The
    // binary resolves both weak slots (`v24` = head, `a1` = tail) up front.
    CUnitCommand* const queueHeadCommand = ResolvePatrolQueueCommand(commandQueue, /*front=*/true);
    CUnitCommand* const queueTailCommand = ResolvePatrolQueueCommand(commandQueue, /*front=*/false);

    STIMap* const map = unit->SimulationRef->mMapData;

    // Leg-start world position: the patrol's bound command (binary `this + 0x54`)
    // resolved to a cell then converted via the owner footprint's layer/size.
    CUnitCommand* const legStartCommand = mBoundCommand;
    const SFootprint& startFootprint = unit->GetFootprint();
    SOCellPos startCell{};
    const SOCellPos* const startPosCell = CUnitCommand::GetPosition(legStartCommand, unit, &startCell);
    const Wm3::Vector3f startPos = COORDS_ToWorldPos(
      map,
      *startPosCell,
      static_cast<ELayer>(static_cast<std::uint8_t>(startFootprint.mOccupancyCaps)),
      startFootprint.mSizeX,
      startFootprint.mSizeZ
    );

    // Leg-end reference position: the queue-tail command's world position when it
    // is a distinct Patrol/FormPatrol command (binary type 16/18); otherwise the
    // owner unit's own current position. The type is read via the named
    // `mVarDat.mCmdType` field, consistent with every other recovered task.
    Wm3::Vector3f endPos;
    if (queueTailCommand != nullptr && queueTailCommand != queueHeadCommand
        && (queueTailCommand->mVarDat.mCmdType == EUnitCommandType::UNITCOMMAND_Patrol
            || queueTailCommand->mVarDat.mCmdType == EUnitCommandType::UNITCOMMAND_FormPatrol)) {
      const SFootprint& tailFootprint = unit->GetFootprint();
      SOCellPos tailCell{};
      const SOCellPos* const tailPosCell = CUnitCommand::GetPosition(queueTailCommand, unit, &tailCell);
      endPos = COORDS_ToWorldPos(
        map,
        *tailPosCell,
        static_cast<ELayer>(static_cast<std::uint8_t>(tailFootprint.mOccupancyCaps)),
        tailFootprint.mSizeX,
        tailFootprint.mSizeZ
      );
    } else {
      endPos = unit->GetPosition();
    }

    // Leg direction (start minus end), flattened onto the XZ plane.
    Wm3::Vector3f direction{startPos.x - endPos.x, 0.0f, startPos.z - endPos.z};

    // Leg half-length gate: the binary squares the flattened deltas (the y term
    // is always 0*0) and takes the sqrt; below 0.001 the direction degenerates
    // to the fixed `+Z` fallback, otherwise it is normalized in place.
    const float legLength = std::sqrt((direction.z * direction.z) + (0.0f * 0.0f) + (direction.x * direction.x));
    // 0x0061B4AF-0x0061B4BC: `fcompi 0.001, len` / `jbe` normalizes, so an
    // unordered length also takes the normalize path.
    if (!(kPatrolLegMinLength > legLength)) {
      Wm3::Vector3f::Normalize(&direction);
    } else {
      direction.x = 0.0f;
      direction.y = 0.0f;
      direction.z = 1.0f;
    }

    // Lateral axis: the leg direction rotated 90 degrees about +Y.
    Wm3::Vector3f lateralAxis;
    MultQuadVec(&lateralAxis, &direction, &kSearchBoxYawQuat);

    // Fill the oriented box. Center is the leg midpoint (each lane * 0.5);
    // axis0 = lateral, axis1 = +Y, axis2 = direction; extents are
    // {guardScanRadius, 100.0, guardScanRadius + legHalfLength}.
    mSearchBox.Center.x = (startPos.x + endPos.x) * 0.5f;
    mSearchBox.Center.y = (endPos.y * 2.0f) * 0.5f;
    mSearchBox.Center.z = (startPos.z + endPos.z) * 0.5f;

    mSearchBox.Axis[0] = lateralAxis;
    mSearchBox.Axis[1] = Wm3::Vector3f{0.0f, 1.0f, 0.0f};
    mSearchBox.Axis[2] = direction;

    mSearchBox.Extent[0] = guardScanRadius;
    mSearchBox.Extent[1] = 100.0f;
    mSearchBox.Extent[2] = guardScanRadius + (legLength * 0.5f);
  }

  /**
   * Address: 0x0061C130 (FUN_0061C130, Moho::CUnitPatrolTask::TaskTick / CTask::Execute slot 1)
   *
   * IDA signature:
   * int __thiscall TaskTick(Moho::CUnitPatrolTask* this);
   *
   * What it does:
   * One patrol tick. Terminates when the navigator is gone or a stalled move is
   * in flight; otherwise, in priority order: hand off to a refuel platform,
   * hand off to a best-enemy attack task, evaluate a patrol reclaim/attack
   * candidate (repair-if-allied else reclaim), or resubmit the patrol navigator
   * goal. Wires all four callees (FindPlatform, FindTarget, the reclaim/attack
   * evaluator, and the arrival test) by name.
   */
  int CUnitPatrolTask::Execute()
  {
    Unit* const unit = mUnit;
    (void)unit->GetBlueprint();

    IAiNavigator* const navigator = unit->AiNavigator;
    if (navigator == nullptr || (mMoving && mNavStalled)) {
      return TASKSTATUS_Done;
    }

    ++mTickCounter;
    if (mTaskState != TASKSTATE_Preparing) {
      return 7;
    }

    // Refuel/repair platform handoff (only when not formation-bound).
    if (mFormationInstance == nullptr) {
      if (Unit* const platform = unit->FindPlatform(); platform != nullptr) {
        if (IAiTransport* const platformTransport = platform->AiTransport; platformTransport != nullptr) {
          platformTransport->TransportResetReservation();
        }
        mDispatch->IssueRefuelTask(platform);
        mMoving = false;
        return 7;
      }
    }

    // Best-enemy attack handoff.
    if (Entity* const enemy = FindTarget(); enemy != nullptr) {
      unit->GuardedPos = unit->GetPosition();

      CAiTarget attackTarget{};
      attackTarget.UpdateTarget(enemy);
      // 0x0061C232..0x0061C23A: the respect-formation factory (0x005F2750),
      // no formation, overcharge off.
      (void)CAttackTargetTask::CreateRespectFormation(mDispatch, &attackTarget, nullptr, false);
      mMoving = false;
      return 7;
    }

    // Patrol reclaim/attack evaluation.
    Entity* const reclaimCandidate = EvaluatePatrolReclaimAttack();
    if (reclaimCandidate == nullptr) {
      if (unit->IsUnitState(UNITSTATE_NeedToTerminateTask)) {
        return TASKSTATUS_Done;
      }
      if (mMoving && PatrolGoalCoversUnitOrigin(*this) && mTickCounter > 3) {
        if (mFormationInstance == nullptr || unit->mInfoCache.mHasFormationSpeedData) {
          return TASKSTATUS_Done;
        }
      } else if (navigator->GetStatus() == AINAVSTATUS_Idle || !mMoving) {
        navigator->SetGoal(mGoal);
        unit->UpdateSpeedThroughStatus();
        mMoving = true;
        mNavStalled = false;
      }
      return 7;
    }

    // Allied unit within range -> repair; otherwise reclaim.
    if (Unit* const candidateUnit = reclaimCandidate->IsUnit(); candidateUnit != nullptr) {
      CArmyImpl* const candidateArmy = reclaimCandidate->ArmyRef;
      if (unit->ArmyRef->GetAllianceWith(candidateArmy) == ALLIANCE_Ally) {
        // Spawn the repair sub-task the binary spawns here (0x0061C130 ->
        // CUnitRepairTask operator new `_0` at 0x005F8DA0 -> ctor 0x005F8C80).
        (void)CUnitRepairTask::Allocate(mDispatch, candidateUnit, false);
        mMoving = false;
        return 7;
      }
    }

    (void)mMembership.Add(reclaimCandidate);
    CAiTarget reclaimTarget{};
    reclaimTarget.UpdateTarget(reclaimCandidate);
    IssueReclaimTask(mDispatch, reclaimTarget);
    mMoving = false;
    return 7;
  }

  /**
   * Address: 0x0061CF50 (FUN_0061CF50, Moho::CUnitPatrolTask::MemberDeserialize)
   *
   * IDA signature:
   * void __usercall MemberDeserialize(CUnitPatrolTask* this@<ecx>, gpg::ReadArchive* archive@<eax>);
   *
   * What it does:
   * Loads patrol-task state from an archive in binary lane order:
   *   1. base `CCommandTask` sub-object (by reflected type).
   *   2. `mDispatch` via `ReadPointer_CCommandTask` (IAiCommandDispatchImpl is a
   *      CCommandTask).
   *   3. `mBoundCommand` via `ReadPointer_CUnitCommand`.
   *   4. `mFormationInstance` via `ReadPointer_IFormationInstance`.
   *   5. `mGoal` (`SNavGoal`) by reflected type.
   *   6. `mMoving` / `mNavStalled` / `mInFormation` via the virtual `ReadBool`
   *      archive slot (vtbl +0x38).
   *   7. `mSearchBox` (`Box3f`) by reflected type.
   *   8. `mTickCounter` via the virtual `ReadInt` archive slot (vtbl +0x24).
   *   9. `mMembership` (`EntitySetTemplate<Entity>`) by reflected type.
   */
  void CUnitPatrolTask::MemberDeserialize(gpg::ReadArchive* const archive)
  {
    if (archive == nullptr) {
      return;
    }

    const gpg::RRef ownerRef{};

    archive->Read(CachedCCommandTaskType(), static_cast<CCommandTask*>(this), ownerRef);

    CCommandTask* dispatchTask = static_cast<CCommandTask*>(mDispatch);
    archive->ReadPointer(&dispatchTask, &ownerRef);
    mDispatch = static_cast<IAiCommandDispatchImpl*>(dispatchTask);

    archive->ReadPointer(&mBoundCommand, &ownerRef);

    archive->ReadPointer(&mFormationInstance, &ownerRef);

    archive->Read(CachedSNavGoalType(), &mGoal, ownerRef);

    archive->ReadBool(&mMoving);
    archive->ReadBool(&mNavStalled);
    archive->ReadBool(&mInFormation);

    archive->Read(CachedBox3fType(), &mSearchBox, ownerRef);
    archive->ReadInt(&mTickCounter);
    archive->Read(CachedEntitySetTemplateEntityType(), &mMembership, ownerRef);
  }

  /**
   * Address: 0x0061D0C0 (FUN_0061D0C0, Moho::CUnitPatrolTask::MemberSerialize)
   *
   * IDA signature:
   * void __usercall MemberSerialize(CUnitPatrolTask* this@<eax>, gpg::WriteArchive* archive@<esi>);
   *
   * What it does:
   * Line-for-line mirror of `MemberDeserialize` over the identical field set and
   * order: base sub-object via reflected `Write`, the three tracked pointer
   * lanes via `RRef_*` + `WriteRawPointer(... Unowned ...)`, the goal payload via
   * reflected `Write`, the three flags via the virtual `WriteBool` slot, the
   * search box via reflected `Write`, the tick counter via the virtual
   * `WriteInt` slot, and the membership node via reflected `Write`.
   */
  void CUnitPatrolTask::MemberSerialize(gpg::WriteArchive* const archive) const
  {
    if (archive == nullptr) {
      return;
    }

    const gpg::RRef ownerRef{};

    archive->Write(CachedCCommandTaskType(), static_cast<const CCommandTask*>(this), ownerRef);

    gpg::RRef pointerRef{};
    (void)gpg::RRef_CCommandTask(&pointerRef, static_cast<CCommandTask*>(mDispatch));
    gpg::WriteRawPointer(archive, pointerRef, gpg::TrackedPointerState::Unowned, ownerRef);

    pointerRef = gpg::MakeRRef<moho::CUnitCommand>(mBoundCommand);
    gpg::WriteRawPointer(archive, pointerRef, gpg::TrackedPointerState::Unowned, ownerRef);

    (void)gpg::RRef_IFormationInstance(&pointerRef, mFormationInstance);
    gpg::WriteRawPointer(archive, pointerRef, gpg::TrackedPointerState::Unowned, ownerRef);

    archive->Write(CachedSNavGoalType(), &mGoal, ownerRef);

    archive->WriteBool(mMoving);
    archive->WriteBool(mNavStalled);
    archive->WriteBool(mInFormation);

    archive->Write(CachedBox3fType(), &mSearchBox, ownerRef);
    archive->WriteInt(mTickCounter);
    archive->Write(CachedEntitySetTemplateEntityType(), &mMembership, ownerRef);
  }

  /**
   * Address: 0x0061C480 (FUN_0061C480, Moho::CUnitPatrolTask::operator new)
   *
   * What it does:
   * Allocates one patrol-task object and forwards constructor arguments into
   * in-place construction.
   */
  CUnitPatrolTask* CUnitPatrolTask::Create(
    CCommandTask* const dispatchTask,
    const void* const goalPayload,
    const bool inFormation
  )
  {
    void* const storage = ::operator new(sizeof(CUnitPatrolTask));
    if (!storage) {
      return nullptr;
    }

    try {
      return ::new (storage) CUnitPatrolTask(dispatchTask, goalPayload, nullptr, inFormation);
    } catch (...) {
      ::operator delete(storage);
      throw;
    }
  }

  /**
   * Address: 0x0061C4E0 (FUN_0061C4E0, Moho::CUnitPatrolTask::operator new `_0` overload)
   * Mangled: ??2CUnitPatrolTask@Moho@@QAE@@Z_0
   *
   * What it does:
   * Formation-instance allocation overload used by
   * `IAiCommandDispatchImpl::DispatchTask` when an existing
   * `IFormationInstance` must be bound to the new patrol task.
   */
  CUnitPatrolTask* CUnitPatrolTask::CreateWithFormation(
    CCommandTask* const dispatchTask,
    const void* const goalPayload,
    IFormationInstance* const formationInstance
  )
  {
    void* const storage = ::operator new(sizeof(CUnitPatrolTask));
    if (!storage) {
      return nullptr;
    }

    try {
      return ::new (storage) CUnitPatrolTask(dispatchTask, goalPayload, formationInstance, false);
    } catch (...) {
      ::operator delete(storage);
      throw;
    }
  }

} // namespace moho


namespace
{

} // namespace

namespace moho
{
  /**
   * Address: 0x0061AD10 (FUN_0061AD10)
   */
  void CUnitPatrolTask::MemberConstruct(gpg::ReadArchive&, const int, const gpg::RRef&, gpg::SerConstructResult& result)
  {
    result.SetUnowned(gpg::MakeRRef(new CUnitPatrolTask()), 0u);
  }

  /**
   * `gpg::SerConstructHelper<CUnitPatrolTask>`, vtable 0x00E20784.
   *
   * Address: 0x00BD1300 (FUN_00BD1300 -- constructs the global and registers its destructor.)
   * Address: 0x00BFA180 (FUN_00BFA180 -- the global's destructor.)
   * Address: 0x0061C660 (FUN_0061C660 -- `Init`.)
   * Address: 0x0061AD00 (FUN_0061AD00 -- `Construct`, a forward to `MemberConstruct`.)
   * Address: 0x0061CBC0 (FUN_0061CBC0 -- `Delete`.)
   */
  struct CUnitPatrolTaskConstruct : gpg::SerConstructHelper<CUnitPatrolTask>
  {};

  /**
   * `gpg::SerSaveLoadHelper<CUnitPatrolTask>`, vtable 0x00E20794.
   *
   * Address: 0x00BD1340 (FUN_00BD1340 -- constructs the global and registers its destructor.)
   * Address: 0x00BFA1B0 (FUN_00BFA1B0 -- the global's destructor.)
   * Address: 0x0061ADC0 (FUN_0061ADC0 -- an unreferenced out-of-line copy of the constructor.)
   * Address: 0x0061C6E0 (FUN_0061C6E0 -- `Init`.)
   * Address: 0x0061ADA0 (FUN_0061ADA0 -- `Deserialize`, a forward to `MemberDeserialize`.)
   * Address: 0x0061ADB0 (FUN_0061ADB0 -- `Serialize`, a forward to `MemberSerialize`.)
   */
  struct CUnitPatrolTaskSerializer : gpg::SerSaveLoadHelper<CUnitPatrolTask>
  {};
} // namespace moho

namespace
{
  // Address: 0x010B19BC -- process-global `CUnitPatrolTaskConstruct` singleton.
  moho::CUnitPatrolTaskConstruct gCUnitPatrolTaskConstruct;

  // Address: 0x010B1A64 -- process-global `CUnitPatrolTaskSerializer` singleton.
  moho::CUnitPatrolTaskSerializer gCUnitPatrolTaskSerializer;
} // namespace
