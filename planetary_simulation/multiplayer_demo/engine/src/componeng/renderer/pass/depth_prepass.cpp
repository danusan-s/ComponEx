#include "componeng/renderer/pass/depth_prepass.hpp"

#include "componeng/renderer/batching/render_queue.hpp"
#include "componeng/renderer/draw/batch_drawer.hpp"

namespace componeng::renderer {

void DepthPrepass::execute(const PassContext &ctx) {
  drawBatches(ctx, this->id(), 0, ctx.queue.opaqueCount());
}

} // namespace componeng::renderer
