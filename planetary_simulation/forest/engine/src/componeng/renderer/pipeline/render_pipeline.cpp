#include "componeng/renderer/pipeline/render_pipeline.hpp"

#include "componeng/renderer/pipeline/pass_profiler.hpp"

namespace componeng::renderer {

RenderPipeline &RenderPipeline::add(std::unique_ptr<RenderPass> pass) {
  m_passes.push_back(std::move(pass));
  return *this;
}

void RenderPipeline::execute(const PassContext &ctx) const {
  for (const auto &pass : m_passes) {
    ctx.profiler.beginPass(pass->id());
    if (pass->enabled(ctx)) {
      pass->execute(ctx);
    }
    // Close the shader timing span here so a pass can't leak it into the next.
    ctx.profiler.timeShader(nullptr);
    ctx.profiler.endPass(pass->id());
  }
}

} // namespace componeng::renderer
