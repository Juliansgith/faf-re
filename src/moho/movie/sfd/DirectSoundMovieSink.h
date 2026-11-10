#pragma once
#include <cstdint>
#include <memory>
#include <span>
struct IDirectSound;
namespace moho::sfd
{
  class AudioSink
  {
  public:
    explicit AudioSink(IDirectSound* device);
    ~AudioSink();
    AudioSink(const AudioSink&) = delete;
    AudioSink& operator=(const AudioSink&) = delete;
    // Pump and all access occur on the player's serialized timeline.
    void Pump();
    std::size_t Write(std::span<const std::int16_t> stereo);
    void Pause(bool paused);
    void Stop();
    void SetVolume(long attenuation);
    double Clock() const;
    double Submitted() const;
    bool Empty() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
  };

#if !defined(_WIN32)
  // Engine addition (faf-neural-ai, Linux client): what CMovieManager holds as its "DirectSound"
  // device. It stands for SDL's audio subsystem with a playback device; each AudioSink is an SDL
  // audio stream (48 kHz 16-bit stereo) on the default device. Null when there is no playback
  // device: movies then play without sound, as with a failed DirectSoundCreate.
  IDirectSound* OpenMovieAudioDevice();
  void CloseMovieAudioDevice(IDirectSound* device);
#endif
} // namespace moho::sfd

#if !defined(_WIN32) && !defined(DSBVOLUME_MIN)
#define DSBVOLUME_MIN (-10000) // <dsound.h>: the attenuation (hundredths of a dB) of silence
#endif
