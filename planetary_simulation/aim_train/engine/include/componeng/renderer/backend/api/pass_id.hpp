#pragma once

#include <cstddef>

namespace componeng::renderer::api {

/**
 * @brief The stages the RenderPipeline draws a frame in, in order. Each is
 * timed on the GPU (IRenderDevice::beginGpuPass) and has its own counters
 * (resources::RenderStats).
 */
enum class PassId : int {
  DepthPrepass, // opaque and cutout batches, depth only
  Opaque,       // opaque and cutout batches, shaded
  Sky,          // the full-screen sky triangle
  Transparent,  // blended / depth-less batches
  PostProcess,  // scale and anti-alias the finished scene onto the window
};

constexpr std::size_t kPassCount = 5;

constexpr const char *passName(PassId pass) {
  switch (pass) {
    case PassId::DepthPrepass:
      return "Depth pre-pass";
    case PassId::Opaque:
      return "Opaque";
    case PassId::Sky:
      return "Sky";
    case PassId::Transparent:
      return "Transparent";
    case PassId::PostProcess:
      return "Post-process";
  }
  return "?";
}

} // namespace componeng::renderer::api
