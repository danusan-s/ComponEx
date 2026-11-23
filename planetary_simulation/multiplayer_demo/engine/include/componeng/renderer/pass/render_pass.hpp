#pragma once

#include "componeng/renderer/backend/api/pass_id.hpp"
#include "componeng/renderer/pipeline/pass_context.hpp"

namespace componeng::renderer {

/**
 * @brief One stage of drawing a frame. A RenderPipeline runs its passes in
 * order.
 *
 * The pipeline brackets every pass for timing and stats (PassProfiler), even
 * a disabled one, so a pass only issues its own work and never has to call
 * beginPass / endPass itself.
 *
 * A pass keeps no GPU state across frames. Anything that must (instance
 * buffers, material binder) lives in RenderSystem and arrives in the
 * PassContext.
 */
class RenderPass {
public:
  virtual ~RenderPass() = default;

  /** Which slot of the GPU timers and RenderStats this pass reports to. */
  virtual api::PassId id() const = 0;

  /** False skips execute() this frame; the pass still reads ~0 in the
   *  profiler. */
  virtual bool enabled(const PassContext &) const {
    return true;
  }

  virtual void execute(const PassContext &ctx) = 0;
};

} // namespace componeng::renderer
