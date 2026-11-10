// Engine addition (faf-neural-ai): the native Linux client's SFD movie decoder, in place of
// moho/movie/sfd/FfmpegSfdDecoder.cpp: the Linux build has no FFmpeg (libavcodec / libavformat /
// libswresample) yet, so opening a movie fails - IndependentSfdBackend reports the failure and the
// game goes on as with a missing movie file. Logged once.

#include "moho/movie/sfd/FfmpegSfdDecoder.h"

#include <atomic>
#include <stdexcept>

#include "gpg/core/utils/Logging.h"

namespace moho::sfd
{
  struct FfmpegDecoder::Impl
  {
  };

  FfmpegDecoder::FfmpegDecoder() = default;
  FfmpegDecoder::~FfmpegDecoder() = default;

  void FfmpegDecoder::Open(const char* const filename, bool)
  {
    static std::atomic<bool> sLogged{false};
    if (!sLogged.exchange(true)) {
      gpg::Warnf("movies: the Linux client is built without FFmpeg; \"%s\" and the other movies do not play",
                 filename != nullptr ? filename : "");
    }
    throw std::runtime_error("SFD movies need FFmpeg, which this Linux build does not have");
  }

  bool FfmpegDecoder::Read(DecodeBatch&)
  {
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
