// Engine addition (faf-neural-ai): the frame-stall sampler on Linux. FrameSampler.cpp samples the main
// thread's call stack with DbgHelp when a frame stalls (FAF_FRAME_SAMPLER, a Windows diagnostic); the
// Linux client has no sampler, so the frame loop's mark does nothing.

#include "gpg/core/diagnostics/FrameSampler.h"

namespace gpg::diag
{
  void FrameSamplerMark() {}
} // namespace gpg::diag
