#pragma once

#include "componeng/renderer/backend/api/pass_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace componeng::resources {

/** What one render pass submitted this frame. */
struct PassStats {
  int drawCalls = 0;
  uint32_t instances = 0;
  uint64_t triangles = 0; // indices / 3, times instances
  int shaderBinds = 0;    // times a different shader was made current
  // Wall time the CPU spent issuing the pass's commands. Measured only in
  // builds with profiling (COMPONENG_PROFILING), otherwise 0. This is
  // submission cost, not GPU time: see IRenderDevice::beginGpuPass for that.
  double cpuMs = 0.0;
};

/**
 * @brief Per-frame counters and CPU timings of the RenderSystem, reset at the
 * start of each of its updates. The counters are always filled in; only the
 * timings depend on the debug UI.
 */
struct RenderStats {
  std::array<PassStats, renderer::api::kPassCount> pass{};

  // Uploading every batch's instance data to the GPU, ahead of the passes.
  std::size_t uploadBytes = 0;
  double uploadCpuMs = 0.0;

  PassStats &of(renderer::api::PassId p) {
    return pass[static_cast<std::size_t>(p)];
  }
  const PassStats &of(renderer::api::PassId p) const {
    return pass[static_cast<std::size_t>(p)];
  }

  /** The passes summed (cpuMs included). */
  PassStats total() const {
    PassStats t;
    for (const PassStats &p : pass) {
      t.drawCalls += p.drawCalls;
      t.instances += p.instances;
      t.triangles += p.triangles;
      t.shaderBinds += p.shaderBinds;
      t.cpuMs += p.cpuMs;
    }
    return t;
  }
};

} // namespace componeng::resources
