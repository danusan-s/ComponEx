#include "componeng/renderer/pass/opaque_pass.hpp"

#include "componeng/renderer/batching/render_queue.hpp"
#include "componeng/renderer/draw/batch_drawer.hpp"

namespace componeng::renderer {

void OpaquePass::execute(const PassContext &ctx) {
  drawBatches(ctx, this->id(), 0, ctx.queue.opaqueCount());
}

} // namespace componeng::renderer
