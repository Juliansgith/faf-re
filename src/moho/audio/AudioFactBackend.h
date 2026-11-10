#pragma once

// Engine addition (faf-neural-ai): the recovered XACT interfaces of AudioEngine.h (IXACTEngine,
// IXACTSoundBank, IXACTWaveBank, IXACTCue) implemented on FAudio's XACT3 runtime, FACT, for the
// native Linux client. AudioEngine.cpp includes this in its Linux branch only and hands
// `fact::Engine` out where Windows creates Microsoft's engine (func_CreateXACTinstance), so the
// engine-facing functions (AudioEngine::Play, SetVolume, Calculate3D, IsStopped, the bank loaders,
// CUserSoundManager's cue handling) are the Windows build's code on either engine.
//
// What differs from XACT, and how it is kept safe here:
//  - FACTAudioEngine_Initialize creates the audio device itself and, when that fails, leaves the
//    engine holding a released FAudio that a later ShutDown or Release would use again, and FAudio's
//    SDL platform never checks that the device opened. The engine below opens the default playback
//    device once to test it, makes the FAudio and its mastering voice itself, and hands both to
//    FACT only when they exist; without a device Initialize fails (XACTENGINE_E_NOAUDIODEVICE) and
//    the caller runs silent (AudioEngine's ctor sets mNoSound), with no crash and nothing leaked.
//  - FACTSoundBank_Play/Prepare do not check the cue index before using the cue they would have
//    made; the bank checks it first (XACTENGINE_E_INVALIDCUEINDEX).
//  - FACT destroys a sound bank's cues with the bank. The cue objects the game still holds are
//    detached then: later calls on them fail or report "stopped", and Destroy frees only the
//    wrapper, where XACT would use freed memory.
//  - Streaming wave banks read through file callbacks on the Win32 shim's file handles (positional
//    reads, one at a time), so the loader opens and closes them exactly as on Windows.
//  - The PrepareWave family is unimplemented in FACT (and unused by the engine): E_NOTIMPL.

#if defined(_WIN32)
#error "moho/audio/AudioFactBackend.h is for the Linux client only"
#endif

#include <atomic>
#include <cstdint>
#include <mutex>
#include <new>
#include <set>

#include <Windows.h>

#include <FACT.h>
#include <FAudio.h>
#include <SDL3/SDL_audio.h>

#include "moho/audio/AudioEngine.h"

namespace moho::fact
{
  inline constexpr std::int32_t kNotImplemented = static_cast<std::int32_t>(0x80004001u);
  inline constexpr std::int32_t kNoInterface = static_cast<std::int32_t>(0x80004002u);
  inline constexpr std::int32_t kOutOfMemory = static_cast<std::int32_t>(0x8007000Eu);
  inline constexpr std::int32_t kInvalidArg = static_cast<std::int32_t>(0x80070057u);
  inline constexpr std::int32_t kNotInitialized = static_cast<std::int32_t>(0x8AC70002u);
  inline constexpr std::int32_t kInvalidUsage = static_cast<std::int32_t>(0x8AC70006u);
  inline constexpr std::int32_t kInvalidCueIndex = static_cast<std::int32_t>(0x8AC7000Cu);
  inline constexpr std::int32_t kNoAudioDevice = static_cast<std::int32_t>(0x8AC70017u);

  [[nodiscard]] inline std::int32_t Result(const std::uint32_t factResult) noexcept
  {
    return static_cast<std::int32_t>(factResult);
  }

  /// Guards every cue wrapper against its sound bank's destruction (one lock for all banks: cue
  /// calls are few per frame and uncontended).
  [[nodiscard]] inline std::mutex& CueLock() noexcept
  {
    static std::mutex lock;
    return lock;
  }

  /// FACT's file callbacks for streaming wave banks: `hFile` is the shim's CreateFileA handle the
  /// loader opened, read at the overlapped offset. The read completes before it returns.
  inline int32_t FACTCALL ReadStreamingFile(
    void* const file,
    void* const buffer,
    const uint32_t bytesToRead,
    uint32_t* const bytesRead,
    FACTOverlapped* const overlapped
  )
  {
    static std::mutex readLock;
    DWORD read = 0u;
    {
      const std::lock_guard<std::mutex> guard(readLock);
      LARGE_INTEGER position{};
      position.QuadPart = static_cast<std::int64_t>(
        static_cast<std::uint64_t>(overlapped->Offset) | (static_cast<std::uint64_t>(overlapped->OffsetHigh) << 32u)
      );
      if (::SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
        (void)::ReadFile(file, buffer, bytesToRead, &read, nullptr);
      }
    }
    overlapped->InternalHigh = reinterpret_cast<void*>(static_cast<std::uintptr_t>(read));
    overlapped->Internal = nullptr; // STATUS_SUCCESS
    if (bytesRead != nullptr) {
      *bytesRead = read;
    }
    return 1;
  }

  inline int32_t FACTCALL GetStreamingFileResult(
    void*, FACTOverlapped* const overlapped, uint32_t* const bytesTransferred, int32_t
  )
  {
    *bytesTransferred = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(overlapped->InternalHigh));
    return 1;
  }

  class SoundBank;

  class Cue final : public IXACTCue
  {
  public:
    Cue(SoundBank* const owner, FACTCue* const cue) noexcept
      : mOwner(owner)
      , mCue(cue)
    {
    }

    std::int32_t __stdcall Play() override
    {
      const std::lock_guard<std::mutex> guard(CueLock());
      return mCue != nullptr ? Result(FACTCue_Play(mCue)) : kInvalidUsage;
    }

    void __stdcall Stop(const std::int32_t stopFlags) override
    {
      const std::lock_guard<std::mutex> guard(CueLock());
      if (mCue != nullptr) {
        (void)FACTCue_Stop(mCue, static_cast<uint32_t>(stopFlags));
      }
    }

    std::int32_t __stdcall GetState(std::int32_t* const outState) override
    {
      if (outState == nullptr) {
        return kInvalidArg;
      }
      const std::lock_guard<std::mutex> guard(CueLock());
      uint32_t state = FACT_STATE_STOPPED; // a cue its bank took with it has stopped
      const std::int32_t result = mCue != nullptr ? Result(FACTCue_GetState(mCue, &state)) : 0;
      *outState = static_cast<std::int32_t>(state);
      return result;
    }

    std::int32_t __stdcall Destroy() override;

    void __stdcall Reserved10() override {}
    void __stdcall Reserved14() override {}
    void __stdcall Reserved18() override {}
    void __stdcall Reserved1C() override {}

    std::int32_t __stdcall SetMatrixCoefficients(
      const std::uint32_t srcChannelCount, const std::uint32_t dstChannelCount, const float* const matrixCoefficients
    ) override
    {
      if (matrixCoefficients == nullptr) {
        return kInvalidArg;
      }
      const std::lock_guard<std::mutex> guard(CueLock());
      return mCue != nullptr ? Result(FACTCue_SetMatrixCoefficients(
                                 mCue, srcChannelCount, dstChannelCount, const_cast<float*>(matrixCoefficients)
                               ))
                             : kInvalidUsage;
    }

    std::uint16_t __stdcall GetVariableIndex(const char* const variableName) override
    {
      const std::lock_guard<std::mutex> guard(CueLock());
      return mCue != nullptr && variableName != nullptr ? FACTCue_GetVariableIndex(mCue, variableName)
                                                        : FACTVARIABLEINDEX_INVALID;
    }

    std::int32_t __stdcall SetVariable(const std::uint16_t variableIndex, const float value) override
    {
      const std::lock_guard<std::mutex> guard(CueLock());
      return mCue != nullptr ? Result(FACTCue_SetVariable(mCue, variableIndex, value)) : kInvalidUsage;
    }

    /// The FACT cue the bank made for this wrapper (under CueLock).
    void Bind(FACTCue* const cue) noexcept
    {
      mCue = cue;
    }

    /// The bank is going away and FACT destroys this cue with it (under CueLock).
    void Detach() noexcept
    {
      mOwner = nullptr;
      mCue = nullptr;
    }

  private:
    SoundBank* mOwner;
    FACTCue* mCue;
  };

  class SoundBank final : public IXACTSoundBank
  {
  public:
    explicit SoundBank(FACTSoundBank* const bank) noexcept
      : mBank(bank)
    {
    }

    [[nodiscard]] FACTSoundBank* Handle() const noexcept
    {
      return mBank;
    }

    std::uint16_t __stdcall GetCueIndex(const char* const cueName) override
    {
      return cueName != nullptr ? FACTSoundBank_GetCueIndex(mBank, cueName) : FACTINDEX_INVALID;
    }

    std::int32_t __stdcall Reserved04() override
    {
      return kNotImplemented;
    }

    std::int32_t __stdcall Reserved08() override
    {
      return kNotImplemented;
    }

    std::int32_t __stdcall Prepare(
      const std::uint16_t cueIndex, const std::uint32_t flags, const std::uint32_t timeOffset, IXACTCue** const outCue
    ) override
    {
      return Start(cueIndex, flags, timeOffset, outCue, false);
    }

    std::int32_t __stdcall Play(
      const std::uint16_t cueIndex, const std::uint32_t flags, const std::uint32_t timeOffset, IXACTCue** const outCue
    ) override
    {
      return Start(cueIndex, flags, timeOffset, outCue, true);
    }

    std::int32_t __stdcall Reserved14() override
    {
      return kNotImplemented;
    }

    void __stdcall Destroy() override
    {
      {
        const std::lock_guard<std::mutex> guard(CueLock());
        for (Cue* const cue : mCues) {
          cue->Detach();
        }
        mCues.clear();
        (void)FACTSoundBank_Destroy(mBank); // destroys this bank's cues, held or not
      }
      delete this;
    }

    /// A held cue's Destroy (under CueLock).
    void Forget(Cue* const cue) noexcept
    {
      mCues.erase(cue);
    }

  private:
    std::int32_t Start(
      const std::uint16_t cueIndex,
      const std::uint32_t flags,
      const std::uint32_t timeOffset,
      IXACTCue** const outCue,
      const bool play
    )
    {
      if (outCue != nullptr) {
        *outCue = nullptr;
      }
      if (!play && outCue == nullptr) {
        return kInvalidArg; // XACT's Prepare requires the cue out-pointer
      }

      uint16_t cueCount = 0u;
      (void)FACTSoundBank_GetNumCues(mBank, &cueCount);
      if (cueIndex >= cueCount) {
        return kInvalidCueIndex;
      }

      Cue* wrapper = nullptr;
      if (outCue != nullptr) {
        wrapper = new (std::nothrow) Cue(this, nullptr);
        if (wrapper == nullptr) {
          return kOutOfMemory;
        }
      }

      const std::lock_guard<std::mutex> guard(CueLock());
      FACTCue* cue = nullptr;
      const std::int32_t result = play
        ? Result(FACTSoundBank_Play(mBank, cueIndex, flags, static_cast<int32_t>(timeOffset), wrapper != nullptr ? &cue : nullptr))
        : Result(FACTSoundBank_Prepare(mBank, cueIndex, flags, static_cast<int32_t>(timeOffset), &cue));
      if (wrapper == nullptr) {
        return result; // fire and forget: FACT destroys the cue when it stops
      }
      if (result < 0 || cue == nullptr) {
        delete wrapper;
        return result < 0 ? result : kInvalidUsage;
      }

      wrapper->Bind(cue);
      mCues.insert(wrapper);
      *outCue = wrapper;
      return result;
    }

    FACTSoundBank* mBank;
    std::set<Cue*> mCues; // the cues the game holds (CueLock)
  };

  inline std::int32_t __stdcall Cue::Destroy()
  {
    std::int32_t result = 0;
    {
      const std::lock_guard<std::mutex> guard(CueLock());
      if (mOwner != nullptr) {
        mOwner->Forget(this);
      }
      if (mCue != nullptr) {
        result = Result(FACTCue_Destroy(mCue));
      }
    }
    delete this;
    return result;
  }

  class WaveBank final : public IXACTWaveBank
  {
  public:
    explicit WaveBank(FACTWaveBank* const bank) noexcept
      : mBank(bank)
    {
    }

    [[nodiscard]] FACTWaveBank* Handle() const noexcept
    {
      return mBank;
    }

    /// The loader closes a streaming bank's file afterwards, as on Windows.
    void __stdcall Destroy() override
    {
      (void)FACTWaveBank_Destroy(mBank);
      delete this;
    }

  private:
    FACTWaveBank* mBank;
  };

  class Engine final : public IXACTEngine
  {
  public:
    /// XACT's engine creation (func_CreateXACTinstance): an engine that still needs Initialize.
    [[nodiscard]] static std::int32_t Create(IXACTEngine** const outEngine)
    {
      if (outEngine == nullptr) {
        return kInvalidArg;
      }
      *outEngine = nullptr;

      FACTAudioEngine* engine = nullptr;
      const std::int32_t result = Result(FACTCreateEngine(0u, &engine));
      if (result != 0 || engine == nullptr) {
        return result != 0 ? result : kOutOfMemory;
      }

      auto* const wrapper = new (std::nothrow) Engine(engine);
      if (wrapper == nullptr) {
        (void)FACTAudioEngine_Release(engine);
        return kOutOfMemory;
      }
      *outEngine = wrapper;
      return 0;
    }

    std::int32_t __stdcall QueryInterface(const void*, void** const outObject) override
    {
      if (outObject != nullptr) {
        *outObject = nullptr;
      }
      return kNoInterface;
    }

    std::uint32_t __stdcall AddRef() override
    {
      return ++mReferences;
    }

    std::uint32_t __stdcall Release() override
    {
      const std::uint32_t references = --mReferences;
      if (references == 0u) {
        ShutDown();
        (void)FACTAudioEngine_Release(mEngine);
        delete this;
      }
      return references;
    }

    std::int32_t __stdcall GetRendererCount(std::uint16_t* const outRendererCount) override
    {
      if (outRendererCount == nullptr) {
        return kInvalidArg;
      }
      return Result(FACTAudioEngine_GetRendererCount(mEngine, outRendererCount));
    }

    std::int32_t __stdcall GetRendererDetails(const std::uint16_t rendererIndex, void* const outRendererDetails) override
    {
      if (outRendererDetails == nullptr) {
        return kInvalidArg;
      }
      return Result(
        FACTAudioEngine_GetRendererDetails(mEngine, rendererIndex, static_cast<FACTRendererDetails*>(outRendererDetails))
      );
    }

    std::int32_t __stdcall GetFinalMixFormat(void* const outWaveFormatExtensible) override
    {
      if (outWaveFormatExtensible == nullptr) {
        return kInvalidArg;
      }
      if (!mInitialized) {
        return kNotInitialized;
      }
      return Result(
        FACTAudioEngine_GetFinalMixFormat(mEngine, static_cast<FAudioWaveFormatExtensible*>(outWaveFormatExtensible))
      );
    }

    std::int32_t __stdcall Initialize(const AudioRuntimeParams* const runtimeParams) override
    {
      if (runtimeParams == nullptr) {
        return kInvalidArg;
      }
      if (mInitialized) {
        return static_cast<std::int32_t>(0x8AC70001u); // XACTENGINE_E_ALREADYINITIALIZED
      }

      // A playback device first: FACT would make the device itself and cannot back out of a
      // failure cleanly (see the header).
      uint16_t deviceCount = 0u;
      (void)FACTAudioEngine_GetRendererCount(mEngine, &deviceCount);
      if (deviceCount == 0u) {
        return kNoAudioDevice;
      }
      // A listed device can still fail to open (a sound server that is not running), and FAudio's
      // SDL platform does not check its open: its mastering voice would then never mix, and every
      // cue would stay "playing". Open the default device once to be sure. (FACTCreateEngine
      // started SDL's audio subsystem.)
      const SDL_AudioDeviceID probe = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
      if (probe == 0u) {
        return kNoAudioDevice;
      }
      SDL_CloseAudioDevice(probe);

      FAudio* audio = nullptr;
      if (FAudioCreate(&audio, 0u, FAUDIO_DEFAULT_PROCESSOR) != 0u || audio == nullptr) {
        return kNoAudioDevice;
      }
      FAudioMasteringVoice* master = nullptr;
      if (FAudio_CreateMasteringVoice(audio, &master, FAUDIO_DEFAULT_CHANNELS, FAUDIO_DEFAULT_SAMPLERATE, 0u, 0u, nullptr) != 0u ||
          master == nullptr) {
        (void)FAudio_Release(audio);
        return kNoAudioDevice;
      }

      // Field by field: FACT's struct is not XACT 2's (FACT.h: "Do not memcpy this").
      FACTRuntimeParameters parameters{};
      parameters.lookAheadTime = runtimeParams->lookAheadTime;
      parameters.pGlobalSettingsBuffer = runtimeParams->pGlobalSettingsBuffer;
      parameters.globalSettingsBufferSize = runtimeParams->globalSettingsBufferSize;
      parameters.globalSettingsFlags = runtimeParams->globalSettingsFlags;
      parameters.globalSettingsAllocAttributes = runtimeParams->globalSettingsAllocAttributes;
      parameters.fileIOCallbacks = runtimeParams->fileIOCallbacks;
      if (parameters.fileIOCallbacks.readFileCallback == nullptr) {
        parameters.fileIOCallbacks.readFileCallback = &ReadStreamingFile;
        parameters.fileIOCallbacks.getOverlappedResultCallback = &GetStreamingFileResult;
      }
      parameters.fnNotificationCallback = runtimeParams->fnNotificationCallback;
      parameters.pRendererID = runtimeParams->pRendererID;
      parameters.pXAudio2 = audio;
      parameters.pMasteringVoice = master;

      const std::int32_t result = Result(FACTAudioEngine_Initialize(mEngine, &parameters));
      if (result != 0) {
        // Rejected before FACT took the device (the global settings did not parse).
        FAudioVoice_DestroyVoice(master);
        (void)FAudio_Release(audio);
        return result;
      }

      mInitialized = true;
      return 0;
    }

    void __stdcall ShutDown() override
    {
      if (mInitialized) {
        mInitialized = false;
        (void)FACTAudioEngine_ShutDown(mEngine);
      }
    }

    std::int32_t __stdcall DoWork() override
    {
      return mInitialized ? Result(FACTAudioEngine_DoWork(mEngine)) : kNotInitialized;
    }

    std::int32_t __stdcall CreateSoundBank(
      const void* const data,
      const std::int32_t sizeBytes,
      const std::uint32_t flags,
      const std::uint32_t allocAttributes,
      IXACTSoundBank** const outSoundBank
    ) override
    {
      if (outSoundBank == nullptr || data == nullptr || sizeBytes <= 0) {
        return kInvalidArg;
      }
      *outSoundBank = nullptr;
      if (!mInitialized) {
        return kNotInitialized;
      }

      FACTSoundBank* bank = nullptr;
      const std::int32_t result = Result(FACTAudioEngine_CreateSoundBank(
        mEngine, data, static_cast<uint32_t>(sizeBytes), flags, allocAttributes, &bank
      ));
      if (result != 0 || bank == nullptr) {
        return result != 0 ? result : kInvalidUsage;
      }
      auto* const wrapper = new (std::nothrow) SoundBank(bank);
      if (wrapper == nullptr) {
        (void)FACTSoundBank_Destroy(bank);
        return kOutOfMemory;
      }
      *outSoundBank = wrapper;
      return 0;
    }

    std::int32_t __stdcall CreateInMemoryWaveBank(
      const void* const data,
      const std::int32_t sizeBytes,
      const std::uint32_t flags,
      const std::uint32_t allocAttributes,
      IXACTWaveBank** const outWaveBank
    ) override
    {
      if (outWaveBank == nullptr || data == nullptr || sizeBytes <= 0) {
        return kInvalidArg;
      }
      *outWaveBank = nullptr;
      if (!mInitialized) {
        return kNotInitialized;
      }

      FACTWaveBank* bank = nullptr;
      const std::int32_t result = Result(FACTAudioEngine_CreateInMemoryWaveBank(
        mEngine, data, static_cast<uint32_t>(sizeBytes), flags, allocAttributes, &bank
      ));
      return Wrap(result, bank, outWaveBank);
    }

    std::int32_t __stdcall CreateStreamingWaveBank(const void* const streamingParams, IXACTWaveBank** const outWaveBank) override
    {
      if (outWaveBank == nullptr || streamingParams == nullptr) {
        return kInvalidArg;
      }
      *outWaveBank = nullptr;
      if (!mInitialized) {
        return kNotInitialized;
      }

      const auto& source = *static_cast<const XACT_STREAMING_PARAMETERS*>(streamingParams);
      if (source.file == nullptr || source.file == INVALID_HANDLE_VALUE) {
        return kInvalidArg;
      }
      FACTStreamingParameters parameters{};
      parameters.file = source.file;
      parameters.offset = source.offset;
      parameters.flags = source.flags;
      parameters.packetSize = source.packetSize;

      FACTWaveBank* bank = nullptr;
      const std::int32_t result = Result(FACTAudioEngine_CreateStreamingWaveBank(mEngine, &parameters, &bank));
      return Wrap(result, bank, outWaveBank);
    }

    std::int32_t __stdcall PrepareWave(
      std::uint32_t, const char*, std::uint16_t, std::uint32_t, std::uint32_t, std::uint8_t, void** const outWave
    ) override
    {
      if (outWave != nullptr) {
        *outWave = nullptr;
      }
      return kNotImplemented;
    }

    std::int32_t __stdcall PrepareInMemoryWave(
      std::uint32_t, const void*, std::uint32_t*, std::uint8_t*, std::uint32_t, std::uint8_t, void** const outWave
    ) override
    {
      if (outWave != nullptr) {
        *outWave = nullptr;
      }
      return kNotImplemented;
    }

    std::int32_t __stdcall PrepareStreamingWave(
      std::uint32_t, const void*, const void*, std::uint32_t, std::uint32_t*, std::uint32_t, std::uint8_t, void** const outWave
    ) override
    {
      if (outWave != nullptr) {
        *outWave = nullptr;
      }
      return kNotImplemented;
    }

    std::int32_t __stdcall RegisterNotification(const AudioNotificationDesc* const notificationDesc) override
    {
      if (notificationDesc == nullptr) {
        return kInvalidArg;
      }
      if (!mInitialized) {
        return kNotInitialized;
      }
      const FACTNotificationDescription description = Translate(*notificationDesc);
      return Result(FACTAudioEngine_RegisterNotification(mEngine, &description));
    }

    std::int32_t __stdcall UnRegisterNotification(const AudioNotificationDesc* const notificationDesc) override
    {
      if (notificationDesc == nullptr) {
        return kInvalidArg;
      }
      if (!mInitialized) {
        return kNotInitialized;
      }
      const FACTNotificationDescription description = Translate(*notificationDesc);
      return Result(FACTAudioEngine_UnRegisterNotification(mEngine, &description));
    }

    std::uint16_t __stdcall GetCategory(const char* const categoryName) override
    {
      return mInitialized && categoryName != nullptr ? FACTAudioEngine_GetCategory(mEngine, categoryName)
                                                     : FACTCATEGORY_INVALID;
    }

    void __stdcall Stop(const std::uint16_t category, const std::int32_t flags) override
    {
      if (mInitialized) {
        // Destroys fire-and-forget cues only; the held ones are stopped.
        (void)FACTAudioEngine_Stop(mEngine, category, static_cast<uint32_t>(flags));
      }
    }

    std::int32_t __stdcall SetVolume(const std::uint16_t category, const float volume) override
    {
      return mInitialized ? Result(FACTAudioEngine_SetVolume(mEngine, category, volume)) : kNotInitialized;
    }

    std::int32_t __stdcall Pause(const std::uint16_t category, const std::int32_t pause) override
    {
      return mInitialized ? Result(FACTAudioEngine_Pause(mEngine, category, pause)) : kNotInitialized;
    }

    std::uint16_t __stdcall GetGlobalVariableIndex(const char* const variableName) override
    {
      return mInitialized && variableName != nullptr ? FACTAudioEngine_GetGlobalVariableIndex(mEngine, variableName)
                                                     : FACTVARIABLEINDEX_INVALID;
    }

    std::int32_t __stdcall SetGlobalVariable(const std::uint16_t variableIndex, const float value) override
    {
      return mInitialized ? Result(FACTAudioEngine_SetGlobalVariable(mEngine, variableIndex, value)) : kNotInitialized;
    }

    std::int32_t __stdcall GetGlobalVariable(const std::uint16_t variableIndex, float* const outValue) override
    {
      if (outValue == nullptr) {
        return kInvalidArg;
      }
      return mInitialized ? Result(FACTAudioEngine_GetGlobalVariable(mEngine, variableIndex, outValue)) : kNotInitialized;
    }

  private:
    explicit Engine(FACTAudioEngine* const engine) noexcept
      : mEngine(engine)
    {
    }

    ~Engine() = default;

    static std::int32_t Wrap(const std::int32_t result, FACTWaveBank* const bank, IXACTWaveBank** const outWaveBank)
    {
      if (result != 0 || bank == nullptr) {
        if (result == 0 && bank == nullptr) {
          return kInvalidUsage;
        }
        if (bank != nullptr) {
          (void)FACTWaveBank_Destroy(bank);
        }
        return result;
      }
      auto* const wrapper = new (std::nothrow) WaveBank(bank);
      if (wrapper == nullptr) {
        (void)FACTWaveBank_Destroy(bank);
        return kOutOfMemory;
      }
      *outWaveBank = wrapper;
      return 0;
    }

    /// XACT's registration names the engine's own bank and cue objects; FACT wants its handles.
    static FACTNotificationDescription Translate(const AudioNotificationDesc& source) noexcept
    {
      FACTNotificationDescription description{};
      description.type = source.type;
      description.flags = source.flags;
      if (source.pSoundBank != nullptr) {
        description.pSoundBank =
          static_cast<SoundBank*>(reinterpret_cast<IXACTSoundBank*>(source.pSoundBank))->Handle();
      }
      if (source.pWaveBank != nullptr) {
        description.pWaveBank = static_cast<WaveBank*>(reinterpret_cast<IXACTWaveBank*>(source.pWaveBank))->Handle();
      }
      // Per-cue and per-wave registrations are never made by the engine; FACT's null is "any".
      description.pCue = nullptr;
      description.pWave = nullptr;
      description.cueIndex = source.cueIndex;
      description.waveIndex = source.waveIndex;
      description.pvContext = source.pvContext;
      return description;
    }

    FACTAudioEngine* mEngine;
    std::atomic<std::uint32_t> mReferences{1u};
    bool mInitialized = false;
  };
} // namespace moho::fact
