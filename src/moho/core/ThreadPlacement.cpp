#include "moho/core/ThreadPlacement.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "gpg/core/utils/Logging.h"
#include "moho/console/CConCommand.h"
#include "moho/misc/StartupHelpers.h"

namespace moho
{
  namespace
  {
    // Placement used when neither `/threadplacement` nor the console variable says otherwise.
    constexpr int kDefaultThreadPlacement = 0;

    struct SLogicalCpu
    {
      WORD mGroup = 0;
      BYTE mNumber = 0;
    };

    /// One physical core: its logical processors (SMT siblings), first one first.
    struct SPhysicalCore
    {
      std::vector<SLogicalCpu> mLogical;
      DWORD mNode = 0;
      int mL3 = 0; // index of the last-level cache it shares (a CCD on Zen); mode 3
    };

    struct SPlacementPlan
    {
      bool mValid = false;
      SLogicalCpu mMain;
      SLogicalCpu mSim;
      SLogicalCpu mIssue;
      std::vector<SPhysicalCore> mWorkerCores; // what is left, in preference order
    };

    std::once_flag sPlanOnce;
    SPlacementPlan sPlan;

    /// The `RelationProcessorCore` / `RelationNumaNode` records, or an empty buffer.
    [[nodiscard]] std::vector<unsigned char> QueryProcessorRelation(const LOGICAL_PROCESSOR_RELATIONSHIP relation)
    {
      DWORD length = 0;
      (void)::GetLogicalProcessorInformationEx(relation, nullptr, &length);
      std::vector<unsigned char> buffer(length);
      if (length == 0 || !::GetLogicalProcessorInformationEx(
            relation, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()), &length)) {
        buffer.clear();
      }
      return buffer;
    }

    template <class Visitor>
    void ForEachProcessorRecord(const std::vector<unsigned char>& buffer, Visitor&& visit)
    {
      std::size_t offset = 0;
      while (offset + sizeof(DWORD) * 2u <= buffer.size()) {
        const auto* const record = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (record->Size == 0u) {
          break;
        }
        visit(*record);
        offset += record->Size;
      }
    }

    [[nodiscard]] std::string FormatCpu(const SLogicalCpu& cpu, const bool multiGroup)
    {
      char text[32];
      if (multiGroup) {
        std::snprintf(text, sizeof(text), "%u:%u", cpu.mGroup, cpu.mNumber);
      } else {
        std::snprintf(text, sizeof(text), "%u", cpu.mNumber);
      }
      return text;
    }

    /// Reads the topology and makes the plan; logs it once.
    void BuildPlan()
    {
      // Only the processors this process may run on, in the group the process started in.
      PROCESSOR_NUMBER current{};
      ::GetCurrentProcessorNumberEx(&current);
      DWORD_PTR processMask = 0;
      DWORD_PTR systemMask = 0;
      if (!::GetProcessAffinityMask(::GetCurrentProcess(), &processMask, &systemMask) || processMask == 0) {
        processMask = ~DWORD_PTR{0};
      }

      std::vector<SPhysicalCore> cores;
      int logicalCount = 0;
      bool multiGroup = false;
      ForEachProcessorRecord(QueryProcessorRelation(RelationProcessorCore), [&](const auto& record) {
        if (record.Relationship != RelationProcessorCore) {
          return;
        }
        SPhysicalCore core;
        for (WORD g = 0; g < record.Processor.GroupCount; ++g) {
          const GROUP_AFFINITY& mask = record.Processor.GroupMask[g];
          multiGroup = multiGroup || mask.Group != 0;
          if (mask.Group != current.Group) {
            continue;
          }
          for (BYTE bit = 0; bit < sizeof(KAFFINITY) * 8u; ++bit) {
            const KAFFINITY one = KAFFINITY{1} << bit;
            if ((mask.Mask & one) != 0u && (processMask & one) != 0u) {
              core.mLogical.push_back(SLogicalCpu{mask.Group, bit});
            }
          }
        }
        if (!core.mLogical.empty()) {
          logicalCount += static_cast<int>(core.mLogical.size());
          cores.push_back(std::move(core));
        }
      });

      // NUMA node of each core, so the dedicated threads and the first workers share a node.
      ForEachProcessorRecord(QueryProcessorRelation(RelationNumaNode), [&](const auto& record) {
        if (record.Relationship != RelationNumaNode) {
          return;
        }
        const GROUP_AFFINITY& mask = record.NumaNode.GroupMask;
        for (SPhysicalCore& core : cores) {
          const SLogicalCpu& first = core.mLogical.front();
          if (first.mGroup == mask.Group && (mask.Mask & (KAFFINITY{1} << first.mNumber)) != 0u) {
            core.mNode = record.NumaNode.NodeNumber;
          }
        }
      });
      std::stable_sort(cores.begin(), cores.end(), [](const SPhysicalCore& a, const SPhysicalCore& b) {
        return a.mNode < b.mNode;
      });

      // Level-3 cache of each core (mode 3 keeps the sim thread and its workers on one).
      int l3Count = 0;
      ForEachProcessorRecord(QueryProcessorRelation(RelationCache), [&](const auto& record) {
        if (record.Relationship != RelationCache || record.Cache.Level != 3) {
          return;
        }
        const GROUP_AFFINITY& mask = record.Cache.GroupMask;
        for (SPhysicalCore& core : cores) {
          const SLogicalCpu& first = core.mLogical.front();
          if (first.mGroup == mask.Group && (mask.Mask & (KAFFINITY{1} << first.mNumber)) != 0u) {
            core.mL3 = l3Count;
          }
        }
        ++l3Count;
      });

      if (cores.size() < 4u) {
        gpg::Logf(
          "ThreadPlacement: %zu physical cores / %d logical processors: too few to give the main, sim and issue "
          "threads a core each, threads stay where the OS puts them",
          cores.size(), logicalCount
        );
        return;
      }

      if (sys_ThreadPlacement == 3 && l3Count > 1) {
        // Mode 3 (engine addition, performance): the sim thread on the last last-level cache (away
        // from core 0's interrupts), its workers on that cache's other cores first, then on that
        // cache's SMT siblings, and only then elsewhere; main and issue on another cache. The
        // workers' phases read what the sim thread has just written, which stays in that cache.
        const int simL3 = cores.back().mL3;
        std::vector<SPhysicalCore> nearCores;
        std::vector<SPhysicalCore> farCores;
        for (const SPhysicalCore& core : cores) {
          (core.mL3 == simL3 ? nearCores : farCores).push_back(core);
        }
        if (nearCores.size() >= 2u && farCores.size() >= 3u) {
          sPlan.mSim = nearCores[1].mLogical.front();
          sPlan.mMain = farCores[1].mLogical.front();
          sPlan.mIssue = farCores[2].mLogical.front();
          sPlan.mWorkerCores.clear();
          for (std::size_t i = 0; i < nearCores.size(); ++i) {
            if (i != 1u) {
              sPlan.mWorkerCores.push_back(nearCores[i]);
            }
          }
          // the nearCores cache's siblings as one-processor cores (not the sim thread's own sibling)
          for (std::size_t i = 0; i < nearCores.size(); ++i) {
            if (i != 1u && nearCores[i].mLogical.size() > 1u) {
              SPhysicalCore sibling;
              sibling.mLogical.assign(nearCores[i].mLogical.begin() + 1, nearCores[i].mLogical.end());
              sibling.mL3 = simL3;
              sPlan.mWorkerCores.push_back(sibling);
            }
          }
          for (std::size_t i = 3; i < farCores.size(); ++i) {
            sPlan.mWorkerCores.push_back(farCores[i]);
          }
          sPlan.mWorkerCores.push_back(farCores[0]);
          for (SPhysicalCore& core : sPlan.mWorkerCores) {
            core.mLogical.resize(1); // siblings are listed on their own above
          }
          sPlan.mValid = true;
        }
      }
      if (!sPlan.mValid) {
        // Core 0 takes most interrupts and DPCs: the dedicated threads start at core 1, and core 0 is
        // the last physical core the workers get.
        sPlan.mMain = cores[1].mLogical.front();
        sPlan.mSim = cores[2].mLogical.front();
        sPlan.mIssue = cores[3].mLogical.front();
        sPlan.mWorkerCores.assign(cores.begin() + 4, cores.end());
        sPlan.mWorkerCores.push_back(cores[0]);
        sPlan.mValid = true;
      }

      std::string workers;
      for (const SPhysicalCore& core : sPlan.mWorkerCores) {
        workers += workers.empty() ? "" : " ";
        workers += FormatCpu(core.mLogical.front(), multiGroup);
        for (std::size_t i = 1; i < core.mLogical.size(); ++i) {
          workers += "/" + FormatCpu(core.mLogical[i], multiGroup);
        }
      }
      gpg::Logf(
        "ThreadPlacement: mode %d, %zu physical cores / %d logical processors; main cpu %s, sim cpu %s, issue cpu %s "
        "(their SMT siblings left idle); worker cores (sim workers from the left, user workers from the right, "
        "siblings after '/'): %s",
        sys_ThreadPlacement, cores.size(), logicalCount, FormatCpu(sPlan.mMain, multiGroup).c_str(),
        FormatCpu(sPlan.mSim, multiGroup).c_str(), FormatCpu(sPlan.mIssue, multiGroup).c_str(), workers.c_str()
      );
    }

    [[nodiscard]] int ResolvePlacementMode()
    {
      if (sys_ThreadPlacement < 0) {
        msvc8::vector<msvc8::string> args;
        if (CFG_GetArgOption("/threadplacement", 1u, &args) && !args.empty()) {
          sys_ThreadPlacement = static_cast<int>(std::strtol(args[0].c_str(), nullptr, 10));
        } else {
          sys_ThreadPlacement = kDefaultThreadPlacement;
        }
      }
      return sys_ThreadPlacement;
    }

    /// Worker `index` of a pool: one physical core per worker while cores last, then their siblings.
    [[nodiscard]] SLogicalCpu WorkerCpu(const bool simPool, const unsigned int index)
    {
      const std::size_t coreCount = sPlan.mWorkerCores.size();
      const std::size_t slot = index % coreCount;
      const SPhysicalCore& core = sPlan.mWorkerCores[simPool ? slot : coreCount - 1u - slot];
      return core.mLogical[(index / coreCount) % core.mLogical.size()];
    }
  } // namespace

  int sys_ThreadPlacement = -1;

  TConVar<int> gTConVar_sys_ThreadPlacement(
    "sys_ThreadPlacement",
    "Where threads run: 0 lets the OS decide, 1 gives the main, sim and issue threads and the workers "
    "ideal processors on separate physical cores, 2 pins them there, 3 pins the sim thread and its workers "
    "to one last-level cache (CCD) first. Applies to threads as they start.",
    &sys_ThreadPlacement
  );

  void PlaceCurrentThread(const EThreadRole role, const unsigned int index)
  {
    const int mode = ResolvePlacementMode();
    if (mode != 1 && mode != 2 && mode != 3) {
      return;
    }
    std::call_once(sPlanOnce, BuildPlan);
    if (!sPlan.mValid) {
      return;
    }

    SLogicalCpu cpu;
    switch (role) {
      case EThreadRole::Main:
        cpu = sPlan.mMain;
        break;
      case EThreadRole::Sim:
        cpu = sPlan.mSim;
        break;
      case EThreadRole::Issue:
        cpu = sPlan.mIssue;
        break;
      case EThreadRole::SimWorker:
        cpu = WorkerCpu(true, index);
        break;
      case EThreadRole::UserWorker:
        cpu = WorkerCpu(false, index);
        break;
    }

    if (mode == 2 || mode == 3) {
      GROUP_AFFINITY affinity{};
      affinity.Group = cpu.mGroup;
      affinity.Mask = KAFFINITY{1} << cpu.mNumber;
      if (!::SetThreadGroupAffinity(::GetCurrentThread(), &affinity, nullptr)) {
        gpg::Warnf("ThreadPlacement: SetThreadGroupAffinity failed (%lu)", ::GetLastError());
      }
    } else {
      PROCESSOR_NUMBER ideal{};
      ideal.Group = cpu.mGroup;
      ideal.Number = cpu.mNumber;
      if (!::SetThreadIdealProcessorEx(::GetCurrentThread(), &ideal, nullptr)) {
        gpg::Warnf("ThreadPlacement: SetThreadIdealProcessorEx failed (%lu)", ::GetLastError());
      }
    }
  }
} // namespace moho
