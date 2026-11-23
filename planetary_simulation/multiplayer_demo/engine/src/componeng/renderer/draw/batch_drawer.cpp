#include "componeng/renderer/draw/batch_drawer.hpp"

#include "componeng/renderer/asset/material.hpp"
#include "componeng/renderer/asset/pbr.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/draw/instance_buffer_cache.hpp"
#include "componeng/renderer/draw/material_binder.hpp"
#include "componeng/renderer/draw/render_state_policy.hpp"
#include "componeng/renderer/pipeline/frame_uniforms.hpp"
#include "componeng/renderer/pipeline/pass_profiler.hpp"
#include "componeng/resources/render_stats.hpp"

namespace componeng::renderer {

BatchDrawer::BatchDrawer(const PassContext &ctx) : m_ctx(ctx) {
}

void BatchDrawer::draw(const RenderBatch &batch, api::PassId pass) {
  api::IRenderDevice &device = m_ctx.device;
  const AssetManager &assets = m_ctx.assets;
  const bool depthOnly = pass == api::PassId::DepthPrepass;

  IMaterial &material = assets.getMaterial(batch.materialID);
  const Mesh &mesh = assets.getMesh(batch.meshID);
  const Shader &colorShader = assets.getShader(material.getShaderID());
  // The pre-pass runs the shader's vertex stage with an empty fragment stage,
  // so nothing the fragment stage reads (albedo, PBR textures) needs setting
  // up. A cutout material's pre-pass keeps the fragment stage's alpha test, so
  // it does read the albedo (and the material's own textures). So does a batch
  // with LOD-fading instances: it discards on the fade pattern, and the
  // pre-pass must cut the same pixels.
  const bool alphaTestedDepth =
      depthOnly && (material.isAlphaTested() || batch.fading);
  const bool readsAlbedo = !depthOnly || alphaTestedDepth;
  const Shader &shader = !depthOnly ? colorShader
                         : alphaTestedDepth
                             ? colorShader.alphaTestedDepthVariant()
                             : colorShader.depthVariant();

  resources::PassStats &ps = m_ctx.stats.of(pass);

  const bool shaderChanged = &shader != m_boundProgram;
  if (shaderChanged) {
    ++ps.shaderBinds;
    // Time is attributed to the shader the batch belongs to, not its depth
    // variant, so a shader's timing covers all of its draws.
    m_ctx.profiler.timeShader(&colorShader);
    shader.use();
    m_boundProgram = &shader;
  }

  device.setRenderState(
      renderStateFor(pass, material.getRenderState(), m_ctx.depthPrepassed));

  if (shaderChanged && readsAlbedo) {
    shader.setInteger("albedoMap", 0);
  }

  if (readsAlbedo) {
    const auto *pbr = dynamic_cast<const PbrMaterial *>(&material);
    MaterialBinder::setTextureToggles(shader, pbr != nullptr);

    const Texture2D &albedo = assets.getTexture(material.getTextureID());
    if (material.getTextureID() != m_boundAlbedo) {
      device.setActiveTextureUnit(0);
      albedo.bind();
      m_boundAlbedo = material.getTextureID();
    }
    if (pbr && !depthOnly) {
      MaterialBinder::bindPbrTextures(device, assets, shader, *pbr, albedo);
    }
  }

  m_ctx.binder.bindResources(device, assets, shader, material, depthOnly);
  MaterialBinder::setUniforms(shader, material);

  mesh.getImpl().bind();

  const api::VertexLayout &layout = material.getVertexLayout();
  if (batch.procedural) {
    // No per-instance data: no instance attributes to enable, the shader
    // reads gl_InstanceID.
    device.drawIndexedInstanced(mesh.indexCount(), batch.instanceCount);
  } else {
    device.setupInstanceAttributes(m_ctx.instances.bufferFor(batch), layout);
    device.drawIndexedInstanced(mesh.indexCount(), batch.instanceCount);
    device.unbindInstanceAttributes(layout);
  }

  ++ps.drawCalls;
  ps.instances += batch.instanceCount;
  ps.triangles +=
      static_cast<uint64_t>(mesh.indexCount() / 3) * batch.instanceCount;
}

void drawBatches(const PassContext &ctx, api::PassId pass, std::size_t first,
                 std::size_t last) {
  BatchDrawer drawer(ctx);
  for (std::size_t i = first; i < last; ++i) {
    drawer.draw(ctx.queue.sorted(i), pass);
  }
}

} // namespace componeng::renderer
