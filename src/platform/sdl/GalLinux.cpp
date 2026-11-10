// Engine addition (faf-neural-ai): what the Linux client needs of the excluded Direct3D backend
// files (src/platform/sdl/sources.cmake) that is not Direct3D at all.
//
// - The abstract gal resource bases' out-of-line constructors and destructors. Windows has them in
//   gpg/gal/backends/d3d9/D3D9BaseClasses.cpp, which includes the whole Direct3D 9 backend; these
//   are the same defaulted definitions.
// - The Direct3D 12 options GpuPathSettings.cpp lists as settings (gpg/gal/PlacedResources.h,
//   gpg/gal/PipelineDiskCache.h). Windows defines them in the Direct3D 12 backend; on Linux they are
//   settings nothing reads, kept so the settings file round-trips between the two builds.
#include "gpg/gal/CubeRenderTarget.hpp"
#include "gpg/gal/EffectTechnique.hpp"
#include "gpg/gal/IndexBuffer.hpp"
#include "gpg/gal/PipelineDiskCache.h"
#include "gpg/gal/PipelineState.hpp"
#include "gpg/gal/PlacedResources.h"
#include "gpg/gal/RenderTarget.hpp"

namespace gpg::gal
{
  EffectTechnique::EffectTechnique() = default;
  EffectTechnique::~EffectTechnique() = default;
  IndexBuffer::IndexBuffer() = default;
  IndexBuffer::~IndexBuffer() = default;
  RenderTarget::RenderTarget() = default;
  RenderTarget::~RenderTarget() = default;
  CubeRenderTarget::CubeRenderTarget() = default;
  CubeRenderTarget::~CubeRenderTarget() = default;
  PipelineState::PipelineState() = default;
  PipelineState::~PipelineState() = default;

  bool gPlacedResources = true;
  bool gSharedBuffers = true;
  bool gPipelineDiskCache = true;
} // namespace gpg::gal
