#include "componeng/renderer/pass/transparent_pass.hpp"

#include "componeng/renderer/batching/render_queue.hpp"
#include "componeng/renderer/draw/batch_drawer.hpp"

namespace componeng::renderer {

void TransparentPass::execute(const PassContext &ctx) {
  drawBatches(ctx, this->id(), ctx.queue.opaqueCount(), ctx.queue.size());
}

} // namespace componeng::renderer
