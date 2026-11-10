# Sim ordering audit: MSVC STL vs libc++

Windows builds the engine with clang-cl against the MSVC STL. Linux builds it with clang 18 against
libc++. Both libraries follow the standard, but the standard leaves some orders up to the
implementation:

| Hazard | Why the order differs |
|---|---|
| `std::sort`, `std::partial_sort`, `std::nth_element`, `std::partition` | Not stable: elements that compare equal come out in an implementation-defined order. |
| `std::make_heap` / `push_heap` / `pop_heap` / `sort_heap`, `std::priority_queue` | Equal elements can sit in any heap position. |
| Iterating a `std::unordered_map` / `unordered_set` | Bucket order depends on `std::hash`, which is identity in libc++ and FNV-1a in the MSVC STL, and on the bucket-count policy, which is primes in libc++ and powers of two in the MSVC STL. |
| A sort or ordered container keyed on a pointer | Addresses depend on the allocator. On x64 most objects come from the host heap (Windows heap vs glibc malloc). `moho::SimBlockLane` covers only formations, projectiles, poses and a few storage lanes. |

If such an order reaches sim state, the sim checksum or an order Lua can see, a Linux server and
Windows clients can desync. The replay and live-game checks only cover the code paths they run, so
this audit reviews every site in the `faf_sim` sources by hand. `tests/linux/OrderGate.py` then
keeps the review current.

`fa::sort` (`src/legacy/algorithms/Sort.h`) is the recovered MSVC8 introsort. It is the same code on
both platforms, and it is the retail order whenever ties matter. It takes pointers:
`fa::sort(v.data(), v.data() + v.size(), comp)`.

## Status

Both fixes below are in (2026-11-10):
- `moho/net/LaunchInfoBuilder.cpp` sorts the player options with `fa::sort`, and its allowlist
  entry is gone.
- `CArmyStatItem::SumCategory` and `CArmyStatItem::ToLua` (`moho/sim/CArmyStats.cpp`) walk the
  blueprint stats in blueprint-ordinal order (`BlueprintStatsInOrdinalOrder`). The map itself, and
  so its layout and save format, is unchanged.

The gate passes with no `fix-needed` entry left. The rest of this report is the audit as it found
the tree.

## Result

The sort-family and unordered-iteration audit (the gate's scope) found 51 sites in `faf_sim`. They
make 48 allowlist keys, because three statements appear twice, word for word, in the same file
(`Common.cpp`, `BeatChangeSet.cpp`, `SpatialOracle.cpp`).

| Class | Sites (keys) | Meaning |
|---|---|---|
| safe | 35 (32) | The comparator is a total order over the elements that occur, equal elements are identical, or the order is used only to search or group. |
| not-sim | 15 (15) | Diagnostics, logs, docs, a cache file, or the remote client's counters. |
| fix-needed | 1 (1) | `moho/net/LaunchInfoBuilder.cpp`: player options sorted by slot index. Ties happen only with malformed launch data. |

One more site needs a fix but is outside the gate's scope: `CArmyStatItem::mBlueprintStats`, a map
keyed on blueprint pointers. Its float sums and Lua table go out in address order. See
[Pointer-keyed ordered containers](#pointer-keyed-ordered-containers-outside-the-gate).

None of the starting spots named for this audit is a live hazard:

| Spot | Finding |
|---|---|
| `gpg/core/algorithms/Cluster.cpp:2479` | safe. `Cluster::Node` is only `{uint8 x, uint8 z}`. The key `int16(x \| z << 8)` is a bijection of the node, so only identical nodes tie, and `std::unique` drops them next. |
| `moho/unit/core/UnitReflection.inl:382` | safe. `DeferredBlipUnlinks::Flush` sorts chain heads and blocks by address only to group runs and to `upper_bound`. Each chain is independent, and splicing never reorders the nodes that stay. The parallel flush already relies on this. |
| `moho/unit/tasks/CUnitPatrolTask.cpp` | not-sim. The two sorts (`want`, `have`) belong to the `sim_PatrolPropCacheCheck` diagnostic, which compares sets and logs. The prop cache's `unordered_set` / `unordered_map` are lookup-only. |
| `moho/ai/CAiReconDBImpl.cpp` | Lookup-only (`gReconBlipIndexes`, `mFirst`). The index is built by walking `mBlipMap`, an `fa::multimap` ordered by `sourceEntityId`, and `try_emplace` keeps that walk's first blip per id. `fa::multimap` is the engine's recovered tree, so the result is the same on both platforms. |
| `moho/resource/CSimResources.cpp` | Lookup-only (`DepositIndex::buckets`). The query walks buckets in `bx`/`bz` order and returns a bool. |
| `moho/path/PathTables.cpp` | Lookup-only (`LoadExact`'s `shared` cluster table, keyed by string). |

## Every site

Paths are relative to `src/`. Line numbers are from `merge-20261110` on 2026-11-10. The allowlist
keys sites by statement text, not line number.

### fix-needed

| Site | Reason | Proposed fix |
|---|---|---|
| `moho/net/LaunchInfoBuilder.cpp:243` `std::sort(playerOptions…, mSlotIndex <)` | Player options are ordered by `PlayerOptions` key minus one. The loop at line 302 then turns them, in that order, into the session's players and command sources, which is sim state. Keys 1..n never tie. A non-integral or non-numeric key does: `GetInteger` truncates 0.5 to 0, and a string gives 0, both producing slot −1. Such keys leave tied armies in library order. | `fa::sort(playerOptions.data(), playerOptions.data() + playerOptions.size(), comp)` (the retail MSVC8 order, ties included). The alternative is to reject launch data whose slot keys are not distinct integers. |

### safe: plain `<` over scalars or values (equal elements are identical)

| Site | Notes |
|---|---|
| `gpg/core/algorithms/Cluster.cpp:182` | Packed `int16` edge words, then `unique`. |
| `remote/wire/OrdinalTable.cpp:51` | Content names (`std::string`), then `unique`. |
| `remote/ContentCheck.cpp:44` | Mod uids, hashed in sorted order. |
| `moho/sim/CVFSImpl.cpp:380` | `SortAndUniquePaths`: by string view, then `unique`. |
| `remote/SpatialOracle.cpp:138`, `:295` | Entity ids (`uint32`). |
| `remote/SyncRouting.cpp:1164` | Reclaim ids for `binary_search`. |
| `remote/ViewFrames.cpp:353`, `:472`, `:859` | View and entity ids (`uint32`), then `unique` where it applies. |
| `moho/net/Common.cpp:103`, `:123` | `NetSpeeds` median and jitter over ≤25 floats. The k-th value is the same whatever order ties take. This is network pacing, not sim state. |
| `remote/SpeedLimit.h:73` | `nth_element` median of the beat-time window. Same reasoning; server pacing. |

### safe: strict total order over the elements that occur

| Site | Notes |
|---|---|
| `gpg/core/algorithms/Cluster.cpp:2479` | See above. |
| `lua/LuaObjectIoLib.inl:61` | `io.dir` uses `STR_FileNameOrderLess`, which breaks case-only ties with `strcmp`. The names in one directory are distinct. Lua-visible, and total. |
| `moho/sim/CVFSImpl.cpp:326` | Mount wildcard matches, also by `STR_FileNameOrderLess`. |
| `platform/posix/Win32Shim.cpp:2282` | POSIX `_findfirst64` listing, also by `STR_FileNameOrderLess`. |
| `moho/sim/CVFSImpl.h:233` | `DISK_FuzzEnumerationOrder` (only with `/enumorderfuzz`): by `strcmp` before the seeded shuffle. |
| `moho/sim/CVFSImpl.cpp:881` | `SearchPathCandidateLess` (`PATH_ReverseComponentLess`, then the directory flag) over one wildcard's candidates. Their resolved paths differ in the last component, so only identical candidates tie. |
| `moho/render/CDecalBuffer.cpp:444` | Expired decals by `mInfo.mObj` descending. This is the decal's id from the id pool, unique among live handles, so it replaces the pointer-ordered `fa::set` walk with a deterministic one. (That it matches retail only approximately is noted in `SimBlockLane.h`; that is a separate parity question.) |
| `remote/BeatChangeSet.cpp:29`, `:46` | Change set by entity id. Each entity is synced once per walk, and the focus-change pass adds only missing ids. |
| `remote/ViewIdMap.cpp:122`, `remote/ViewFrames.cpp:479`, `:482`, `:529`, `:580` | Frame records and changes by view id (unique in a frame). |
| `remote/FrameBuilder.cpp:427`, `:857` | Unit stats by name ordinal. A unit's stat tree holds each name once. Wire data, not sim state. |

### safe: sorted by address only to search or group

| Site | Notes |
|---|---|
| `lua/LuaObjectPlusRefs.inl:1141` | Lua GC `ListStarts`: `tmudata` objects by address for `binary_search` membership. The order of `starts` is untouched. |
| `lua/LuaObjectVm.inl:3665` | Library C-function pointers for `binary_search` (`LUA_IsLibraryCFunction`). |
| `moho/unit/core/UnitReflection.inl:382`, `:383` | See above. |
| `remote/SpatialOracle.cpp:228` | Cell entries by (layer, z, x). Entities in one cell tie, but `CollectViewEntities` sorts the admitted ids afterwards (`:295`), so the output does not depend on tie order. |
| `remote/ViewFrames.cpp:780` (unordered iteration) | `mRetiring` is walked in hash order into `mRetired`, which feeds `CViewIdMap::RetireEntities`. Every retired id gets the same grace expiry, and view serials only count up and are never reused. The order changes `mGrace`'s push order and nothing that is read. |

### not-sim

| Site | Notes |
|---|---|
| `lua/LuaObjectBaseLib.inl:1701`, `lua/LuaObjectPlusRefs.inl:27`, `:1244`, `:1245` | `lua_GcVerify` comparisons (sorted sets, logged). |
| `moho/lua/CScrLuaInitForm.cpp:179` | `DumpDocs`: logged binding docs. |
| `moho/lua/ScriptChunkCache.cpp:344` (unordered iteration) | Order of entries in the compiled-chunk cache file. Reading it back is keyed. |
| `moho/sim/SimEntities.inl:994` | `Sim::DumpUnits` console command. Units per blueprint by count; ties are possible, but the output is only logged. The comment above it, saying `std::sort` is "binary-equivalent", is wrong for ties. That matters only if the log should match retail. |
| `moho/unit/tasks/CUnitPatrolTask.cpp:732`, `:733` | `sim_PatrolPropCacheCheck` diagnostic. |
| `remote/client/SyncRebuilder.cpp:747`, `:752`, `:758`, `:761`, `:867`, `:932` (unordered iteration) | The remote client's mismatch counters over the packet's id indexes. Sums, so order-free. |

### Unordered containers with no iteration (lookup-only, not listed)

These were checked by hand and have no iteration site: `CAiReconDBImpl` (`gReconBlipIndexes`,
`mFirst`), `CSimResources` (`buckets`), `PathTables`/`Cluster::LoadExact` (`shared`),
`CUnitPatrolTask` (`sPropLogLatest`, `mMembers`, `sPatrolCaches`), `Entity` (`mExists`),
`CEffectManagerImpl` (`mIndex`), `CScrLuaObjectFactory` (`tCopiedTables`, `sListings`),
`CVFSImpl` (`mFound`, `sNames`), `RRuleGameRules` (`mFound`), `SimKeyframe` (`sRestoredBlocks`),
`AgentObserve` (`facts`), `LuaObjectSerialize` (`tLiveStrings`), `ProfileNames`, `Tss`,
`FileWaitHandleSet`, `Win32Shim` (`sPathSpellingSeen`, `gApcQueues`), and the `remote/` id maps
(`ViewIdMap`, `FrameBuilder`, `PoseTracker`, `SyncRouting`, `UnderlayPaths`, `SessionTables`,
`OrdinalTable`, `GameLinks`, `OracleGolden`, `SyncRebuilder::mHeld`).

## Pointer-keyed ordered containers (outside the gate)

A `std::map`/`fa::map`/`fa::set` keyed on a pointer has the same hazard as a sort keyed on a pointer:
it iterates in address order. In the `faf_sim` closure:

| Container | Iterated? | Class |
|---|---|---|
| `moho/sim/CArmyStats.h:40` `ArmyBlueprintStatTree = fa::map<const RUnitBlueprint*, float>` (`CArmyStatItem::mBlueprintStats`) | Yes: `SumCategory`, `ToLua`, `ArmyXmlStatsNode`, `DumpBlueprintStatLanes`, `CollectBlueprintStatKeys` | **fix-needed** |
| `moho/sim/CArmyStats.cpp:749` `fa::set<const RUnitBlueprint*> blueprintKeys` | Yes: the end-of-game XML stats | not-sim (report order only) |
| `moho/render/CDecalBuffer.h:35` `CDecalBucketSet = fa::set<CDecalHandle*>` | Yes, but x64 re-sorts by `mObj` (see above) | safe |
| `gpg/core/containers/WriteArchive.h:314` `fa::map<const RType*, int> mRefCounts` | No (lookup) | safe |
| `moho/resource/ResourceManager.h:279` `mFactories` (`fa::map<const RType*, …>`) | No (lookup, erase) | safe |
| `moho/audio/CSndParams.cpp:107` `SharedAmbientLoopMap` | No (lookup) | safe |
| `gpg/core/reflection/Reflection.h:370` `TypeMap` (`const char*`, `CStrLess`) | Keyed by string content | safe |
| `gpg/core/reflection/Reflection.h:372` `TypeInfoMap` (`type_info::before`) | No (lookup). `before` is itself implementation-defined. | safe |

**`CArmyStatItem::mBlueprintStats`.** Blueprints come from the host heap, so their relative address
order is not the same on Linux and Windows.
- `SumCategory` adds the per-blueprint floats in that order. Integer counts (`Units_Active`,
  `Units_Killed`, `Enemies_Killed`) are exact in any order below 2^24. The damage stats
  (`Units_TotalDamageDealt` / `…Receive`) have fractions, so their sum can round differently. The
  sum is Lua-visible through `brain:GetBlueprintStat`, and it feeds stat triggers
  (`ResolveConditionValue` → `OnStatsTrigger`).
- `ToLua` builds the `Blueprints` table in the same order. In Lua 5.0, insertion order can decide
  where colliding string keys land, and so the `pairs()` order of `GetArmyStat(...).Blueprints`.

**Proposed fix:** order the map by blueprint ordinal. Give `ArmyBlueprintStatTree` a comparator on
`RBlueprint::mBlueprintOrdinal` (+0x5C), which is assigned in load order and unique per blueprint.
The map stays a 0x0C header triple, the serializer writes entries in iteration order, and loading
re-inserts them, so saves are unaffected. If the type must keep `std::less<pointer>`, sort a
snapshot by ordinal in `SumCategory` and `ToLua` instead. Retail iterated in its own heap's address
order, so no fix reproduces retail exactly. The ordinal order is the same on every platform.

## The gate: `tests/linux/OrderGate.py`

```
python3 tests/linux/OrderGate.py tests/linux/order_allowlist.txt [--root <repo>] [--strict] [--list]
```

The gate is source-based. It needs no build and also runs on Windows; it takes about 5 to 10
seconds. It works in four steps:

1. **Translation units.** It reproduces `cmake/FafLinux.cmake`'s list: `FAF_ENGINE_SOURCES` from
   `src/sources.cmake`, filtered by `src/sim_sources.cmake`'s client-only and sim patterns and by the
   `list(FILTER FAF_SIM_SOURCES EXCLUDE REGEX …)` lines, plus `FAF_REMOTE_SOURCES`,
   `FAF_REMOTE_CLIENT_SOURCES` and every `target_sources(faf_sim PRIVATE …)` file. Today that is 804
   units. It parses the CMake files, so new sources and filters are picked up.
2. **Headers.** It follows `#include`s that resolve inside `src/` (relative to the file, then `src/`,
   then `src/platform/posix/win32`). That adds about 1,000 headers and `.inl` files, such as
   `UnitReflection.inl`, `SimEntities.inl` and the `lua/*.inl` pieces. Third-party headers
   (WildMagic, boost 1.34.1) are not scanned; the ones the sim includes contain no sort or unordered
   container.
3. **Sites.** It strips comments and string literals, then finds:
   - **sort**: `std::` or `std::ranges::` `sort`, `partial_sort`, `partial_sort_copy`,
     `nth_element`, `partition`, `make_heap`, `push_heap`, `pop_heap`, `sort_heap`, and
     `std::priority_queue`. `stable_sort`, `stable_partition` and `fa::sort` are not hazards.
   - **iterate**: a range-for over, `.begin()`/`->begin()`/`cbegin()` of, or free `begin(x)` of, a
     name declared with an unordered type. The declaration can be direct, through a `using`/`typedef`
     alias, or `auto x = <unordered name>`, and must be visible from some translation unit that
     includes the file. If the name's nearest declaration in the same file is a non-unordered type
     (a local `map`, a `names` vector), the name is skipped.
4. **Allowlist.** Each site's key is `<path> | <kind> | <source line from its start to the first '{'
   or ';', whitespace collapsed>`. Moving code keeps the key; changing the call needs a new review.
   An unlisted site fails the gate, which prints the key line to add. Each entry carries the audit
   class (`safe`, `not-sim`, `fix-needed`). `--strict` also fails on `fix-needed` entries and on
   entries that match no site. `--list` prints every site with its line and class.

Verified on 2026-11-10:
- Against this tree, the gate passes (exit 0) and reports the single `fix-needed` entry.
- `--strict` fails (exit 1) on that entry, as intended.
- On a scratch copy with injected code, it reported `std::sort`, a multi-line `std::nth_element`,
  `std::priority_queue`, a range-for over a `std::unordered_map` parameter, and a range-for and
  `.begin()` on a member declared through a `using ProbeSet = std::unordered_set<int>` alias. It
  ignored the same text in a comment and in a string, `std::stable_sort`, and a range-for over a
  local `std::vector` named `map`.

**Limits** (the reasons this audit was done by hand first):
- Unordered iteration is found by name. It misses iteration through an unnamed temporary, a
  template parameter, an iterator passed out of a function, or a range algorithm that takes the
  container itself (`std::ranges::for_each(m, …)`, `std::ranges::copy(m, …)`).
- A sort reached through a project wrapper that calls `std::sort` inside a template is reported once,
  at the wrapper (`DISK_FuzzEnumerationOrder` is one), not at each caller.
- Pointer-keyed ordered containers (previous section) are not gated.
- `#if` branches are all scanned, including branches that do not build on Linux. This errs toward
  reporting more.

### CMake wiring (not applied)

Next to `faf_sim_libm_gate` in `cmake/FafLinux.cmake`:

```cmake
# The order gate: the faf_sim sources order nothing through an implementation-defined algorithm
# (non-stable sorts, heap algorithms, unordered-container iteration) outside the reviewed list,
# so a Linux server orders as Windows does (docs/SIM_ORDER_AUDIT.md). Source-based: it reads
# src/sources.cmake, src/sim_sources.cmake and this file's faf_sim lists itself.
add_custom_target(faf_sim_order_gate
  COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/linux/OrderGate.py"
          "${PROJECT_SOURCE_DIR}/tests/linux/order_allowlist.txt" --root "${PROJECT_SOURCE_DIR}"
  VERBATIM
  COMMENT "Checking faf_sim's ordering sites against tests/linux/order_allowlist.txt")
add_dependencies(headless faf_sim_order_gate)
# Once the fix-needed entries are fixed, the strict form keeps the list clean too:
add_test(NAME sim_order_gate
  COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/linux/OrderGate.py"
          "${PROJECT_SOURCE_DIR}/tests/linux/order_allowlist.txt" --root "${PROJECT_SOURCE_DIR}" --strict)
```

A custom target with no outputs runs on every build. That is about 5 to 10 seconds, the price of not
tracking headers. The gate does not need the Linux toolchain, so the same target can also go in
`src/CMakeLists.txt` for the Windows build, with `python` in place of `python3`.

## Follow-ups

1. Fix the two `fix-needed` items: `LaunchInfoBuilder` → `fa::sort`, and `ArmyBlueprintStatTree` →
   ordinal comparator. Then remove the `LaunchInfoBuilder` allowlist line and switch the test to
   `--strict`.
2. Gate pointer-keyed ordered containers too. Flag `map`/`set`/`multimap`/`multiset` whose key type
   is a pointer and that are iterated, as `OrderGate.py` does for unordered containers.
3. Lua `pairs()` over keys that are tables, functions, userdata or light userdata hashes pointer bits
   (`luaH_mainposition` → `ValuePointerBits` → `value.p.Bits()`, `lua/LuaPrimitives.h:333`). Whether
   that order is the same on both platforms depends on how the Lua heap hands out addresses. This
   audit did not cover it, and it is worth confirming separately.
