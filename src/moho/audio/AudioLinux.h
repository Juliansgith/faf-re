#pragma once

// Engine addition (faf-neural-ai): the native Linux client's sound (FAF_LINUX_CLIENT, no _WIN32).
//
// The Windows build reaches XACT through the recovered XACT 2 interfaces in AudioEngine.h
// (IXACTEngine, IXACTSoundBank, IXACTWaveBank, IXACTCue) on Microsoft's engine, and types the
// engine code names come from the June 2010 SDK's <xact3.h> and <mmreg.h>. On Linux the same
// interfaces are implemented on FAudio's XACT3 runtime, FACT (AudioFactBackend.h), so every
// engine-facing function in AudioEngine.cpp and CUserSoundManager.cpp runs unchanged. This header
// gives those SDK names their FACT equivalents, with the same field names and constant values:
//
//   XACT_NOTIFICATION / _CALLBACK   FACTNotification / FACTNotificationCallback (same layout and
//                                   type numbers; FACT calls func_HandleSoundEvent directly)
//   XACT_NOTIFICATION_DESCRIPTION   XACT's own layout, with the engine's interface pointers; the
//                                   FACT engine adapter swaps them for FACT's handles
//   XACT_FILEIO_CALLBACKS           FACTFileIOCallbacks (left null: FACT's own file reader)
//   XACT_STREAMING_PARAMETERS       XACT's layout; `file` is an FAudioIOStream* (FAudio_fopen)
//   WAVEFORMATEXTENSIBLE            FAudioWaveFormatExtensible (GetFinalMixFormat)

#if defined(_WIN32)
#error "moho/audio/AudioLinux.h is for the Linux client only"
#endif

#include <cstdint>

#include <Windows.h>

#include <FACT.h>

struct IXACT3SoundBank;
struct IXACT3WaveBank;
struct IXACT3Cue;
struct IXACT3Wave;

#pragma pack(push, 1)
/// XACT3's notification registration, as <xact3.h> lays it out. The pointers are the engine's
/// recovered interfaces (moho::IXACTSoundBank and friends); FactEngine::RegisterNotification maps
/// them to FACT's own handles.
struct XACT_NOTIFICATION_DESCRIPTION
{
  std::uint8_t type;
  std::uint8_t flags;
  IXACT3SoundBank* pSoundBank;
  IXACT3WaveBank* pWaveBank;
  IXACT3Cue* pCue;
  IXACT3Wave* pWave;
  std::uint16_t cueIndex;
  std::uint16_t waveIndex;
  void* pvContext;
};
#pragma pack(pop)

using XACT_NOTIFICATION = FACTNotification;
using XACT_NOTIFICATION_CALLBACK = FACTNotificationCallback;
using XACT_FILEIO_CALLBACKS = FACTFileIOCallbacks;
using XACT_RUNTIME_PARAMETERS = FACTRuntimeParameters;

/// XACT's streaming wave-bank parameters. `file` is the FAudioIOStream* FACT's default reader
/// reads from (the Windows build passes a CreateFile handle).
struct XACT_STREAMING_PARAMETERS
{
  HANDLE file;
  DWORD offset;
  DWORD flags;
  WORD packetSize;
};

inline constexpr std::uint8_t XACTNOTIFICATIONTYPE_WAVEBANKPREPARED = 17;

using WAVEFORMATEXTENSIBLE = FAudioWaveFormatExtensible;
