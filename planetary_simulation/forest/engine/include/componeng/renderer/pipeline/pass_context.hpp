#pragma once

#include "componeng/ecs/entity.hpp"

namespace componeng::ecs {
class World;
}

namespace componeng::resources {
struct RenderStats;
}

namespace componeng::renderer {

class AssetManager;
class FrameUniforms;
class InstanceBufferCache;
class MaterialBinder;
class PassProfiler;
class RenderQueue;
namespace api {
class IRenderDevice;
class IRenderTarget;
} // namespace api

/**
 * @brief What every pass of a frame needs, gathered so a pass takes one
 * argument. Built once per frame by RenderSystem and handed to every pass of
 * the RenderPipeline.
 *
 * `instances` and `binder` live across frames in RenderSystem, because they
 * own GPU buffers. A pass only borrows them.
 */
struct PassContext {
  ecs::World &world;
  ecs::EntityID camera;
  api::IRenderDevice &device;
  const AssetManager &assets;
  const FrameUniforms &frame;
  PassProfiler &profiler;
  resources::RenderStats &stats;
  const RenderQueue &queue;
  const InstanceBufferCache &instances;
  MaterialBinder &binder;
  /** A depth pre-pass runs this frame, so the opaque pass shades only the
   *  nearest surface of each pixel (see renderStateFor). */
  bool depthPrepassed;
  /** The off-screen target the scene is drawn into this frame, for a pass
   *  that post-processes it onto the window; null when the scene is drawn
   *  straight to the window. */
  api::IRenderTarget *sceneTarget;
  /** Smooth the edges while copying the scene target to the window. Off, the
   *  copy only scales it (a render scale other than 1). */
  bool fxaa;
};

} // namespace componeng::renderer
