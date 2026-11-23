#pragma once

#include "componeng/renderer/backend/api/pass_id.hpp"

#include <chrono>

namespace componeng::renderer {

class Shader;
namespace api {
class IRenderDevice;
}

} // namespace componeng::renderer

namespace componeng::resources {
struct RenderStats;
}

namespace componeng::renderer {

/**
 * @brief Measurement for one frame of passes: GPU timestamps per pass and per
 * shader, and the CPU time spent issuing each pass.
 *
 * Every call does nothing in builds without the debug UI, so a pass can call
 * it unconditionally and no timer queries are issued.
 *
 * Every pass is bracketed every frame, even an empty one, so a pass that did
 * nothing reads ~0 instead of keeping an old value.
 */
class PassProfiler {
public:
  PassProfiler(api::IRenderDevice &device, resources::RenderStats &stats);

  void beginPass(api::PassId pass);
  void endPass(api::PassId pass);

  /** Time the work issued from here on against `next` (nullptr: against no
   *  shader). A span runs until the next call, so a shader that is bound
   *  several times a frame (opaque, sky, blended) gets its spans summed. */
  void timeShader(const Shader *next);

  /** CPU time spent uploading instance data ahead of the passes. */
  void beginUpload();
  void endUpload();

private:
  using Clock = std::chrono::steady_clock;

  api::IRenderDevice &m_device;
  resources::RenderStats &m_stats;
  const Shader *m_timedShader = nullptr;
  Clock::time_point m_passStart{};
  Clock::time_point m_uploadStart{};
};

} // namespace componeng::renderer
