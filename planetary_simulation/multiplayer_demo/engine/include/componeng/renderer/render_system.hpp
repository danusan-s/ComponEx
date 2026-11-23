#pragma once

#include "componeng/ecs/system.hpp"
#include "componeng/renderer/draw/instance_buffer_cache.hpp"
#include "componeng/renderer/draw/material_binder.hpp"
#include "componeng/renderer/pipeline/frame_uniforms.hpp"
#include "componeng/renderer/pipeline/render_pipeline.hpp"

#include <memory>

namespace componeng::renderer {

/**
 * @brief Presentation-phase system that draws the frame from the RenderQueue
 * BatchingSystem built: depth pre-pass, opaque, sky, transparent and then
 * the post-process pass, in that order. With anti-aliasing on or a render scale
 * other than 1, everything before the post-process pass goes into an
 * off-screen target that it copies onto the window.
 *
 * It builds the RenderPipeline once (onCreate), and each frame gathers the
 * per-frame values, uploads the instance data and runs the pipeline. What each
 * piece does lives beside it:
 *  - renderer/pass/:     one RenderPass per stage of the frame.
 *  - renderer/pipeline/: RenderPipeline (runs the passes), PassContext (what a
 *                        pass gets), FrameUniforms (per-frame values),
 *                        PassProfiler (timing).
 *  - renderer/draw/:     what the batch passes share: BatchDrawer (one batch's
 *                        draw), InstanceBufferCache (instance data upload),
 *                        MaterialBinder (material data), renderStateFor
 *                        (pipeline state per pass).
 *
 * Fills resources::RenderStats every frame: per-pass draw, instance, triangle
 * and shader-bind counts, plus (with the debug UI) per-pass CPU submit time.
 * With the debug UI it also times each pass on the GPU, see
 * IRenderDevice::beginGpuPass.
 */
class RenderSystem final : public ecs::ISystem {
public:
  void onCreate(const ecs::SystemState &state) override;
  void onUpdate(const ecs::SystemState &state) override;
  void onDestroy(const ecs::SystemState &state) override;

private:
  /** The target the scene is drawn into this frame, or null to draw straight
   *  to the window: no anti-aliasing, render scale 1, or no shader for them.
   *  `fxaa` says whether the copy to the window should smooth edges. Kept
   *  across frames and remade when its size changes. */
  api::IRenderTarget *sceneTargetFor(ecs::World &world, bool &fxaa);

  RenderPipeline m_pipeline;
  std::unique_ptr<api::IRenderTarget> m_sceneTarget;
  bool m_warnedMissingPostProcess = false;
  // These live across frames because they own GPU buffers, or remember what
  // was already reported.
  FrameUniforms m_frame;
  InstanceBufferCache m_instances;
  MaterialBinder m_binder;
};

} // namespace componeng::renderer
