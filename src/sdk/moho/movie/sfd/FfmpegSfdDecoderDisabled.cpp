// Build option, not recovered from the binary: the FfmpegDecoder for an engine built with
// /p:FafFfmpeg=false, which links no FFmpeg libraries and ships no FFmpeg DLLs. Every Open fails, so
// IndependentSfdBackend's Player::Open reports the movie as unplayable, as it does for a missing
// file. Meant for headless/automated use where no .sfd movie is ever watched.
#include "moho/movie/sfd/FfmpegSfdDecoder.h"

#include <stdexcept>

namespace moho::sfd
{
  struct FfmpegDecoder::Impl
  {
  };

  FfmpegDecoder::FfmpegDecoder() = default;
  FfmpegDecoder::~FfmpegDecoder() = default;

  void FfmpegDecoder::Open(const char* const filename, const bool decodeAudio)
  {
    (void)decodeAudio;
    throw std::runtime_error(
      std::string("movie playback is disabled in this build (FafFfmpeg=false): ") + (filename ? filename : "")
    );
  }

  bool FfmpegDecoder::Read(DecodeBatch& batch)
  {
    (void)batch;
    return false;
  }

  bool FfmpegDecoder::HasAudio() const
  {
    return false;
  }

  int FfmpegDecoder::Width() const
  {
    return 0;
  }

  int FfmpegDecoder::Height() const
  {
    return 0;
  }

  int FfmpegDecoder::AudioSampleRate() const
  {
    return 0;
  }

  int FfmpegDecoder::AudioChannels() const
  {
    return 0;
  }
} // namespace moho::sfd
