#include "componeng/renderer/pass/post_process_pass.hpp"

#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/draw/render_state_policy.hpp"
#include "componeng/renderer/pipeline/pass_profiler.hpp"
#include "componeng/resources/render_stats.hpp"

namespace componeng::renderer {

void PostProcessPass::execute(const PassContext &ctx) {
  if (m_shaderID == 0) {
    m_shaderID = ctx.assets.getShaderID("post_process");
  }
  const Shader &shader = ctx.assets.getShader(m_shaderID);
  api::IRenderTarget &scene = *ctx.sceneTarget;

  ctx.device.setRenderTarget(nullptr);

  ctx.profiler.timeShader(&shader);
  shader.use();
  shader.setInteger("sceneColor", 0);
  shader.setFloat("fxaaOn", ctx.fxaa ? 1.0f : 0.0f);
  shader.setVector2f("invResolution", 1.0f / static_cast<float>(scene.width()),
                     1.0f / static_cast<float>(scene.height()));
  ctx.device.setActiveTextureUnit(0);
  scene.colorTexture().bind();

  ctx.device.setRenderState(renderStateFor(
      api::PassId::PostProcess, api::RenderState{}, /*depthPrepassed=*/false));
  ctx.device.drawFullscreenTriangle();

  resources::PassStats &ps = ctx.stats.of(api::PassId::PostProcess);
  ++ps.drawCalls;
  ++ps.instances;
  ++ps.triangles;
  ++ps.shaderBinds;

  ctx.device.setRenderState(api::RenderState{});
}

} // namespace componeng::renderer
