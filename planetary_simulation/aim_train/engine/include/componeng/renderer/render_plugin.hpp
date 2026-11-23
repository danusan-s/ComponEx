#pragma once

#include "componeng/core/plugin.hpp"

namespace componeng::renderer {

// Drawing: the mesh, material, light, skybox and LOD components, the asset
// manager with the engine's built-in shaders, textures, materials and meshes,
// and the LOD, culling, batching and render systems. Requires WindowPlugin for
// the render device.
class RenderPlugin final : public core::IPlugin {
public:
  void build(core::Engine &engine) override;
  void ready(ecs::World &world) override;
  void cleanup(ecs::World &world) override;
};

} // namespace componeng::renderer
