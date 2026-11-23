#include "componeng/renderer/render_plugin.hpp"

#include "componeng/core/engine.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/core/window_plugin.hpp"
#include "componeng/renderer/asset/diffuse.hpp"
#include "componeng/renderer/asset/pbr.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/batching/batching_system.hpp"
#include "componeng/renderer/component/light_component.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/renderer/component/procedural_instances_component.hpp"
#include "componeng/renderer/component/skybox_component.hpp"
#include "componeng/renderer/culling/culling_system.hpp"
#include "componeng/renderer/lod/lod_component.hpp"
#include "componeng/renderer/lod/lod_system.hpp"
#include "componeng/renderer/render_system.hpp"
#include "componeng/resources/fog.hpp"
#include "componeng/resources/render_settings.hpp"
#include "componeng/utils/logger.hpp"
#include "componeng/utils/utils.hpp"

namespace componeng::renderer {

void RenderPlugin::build(core::Engine &engine) {
  engine.require<core::WindowPlugin>();
  ecs::World &world = engine.world();

  world.registerComponents<core::TransformComponent, MeshComponent,
                           MaterialComponent, ColorComponent,
                           DirectionalLightComponent, SkyboxComponent,
                           LodComponent, ProceduralInstancesComponent>();

  world.setResource<resources::Fog>(resources::Fog{});
  world.setResource<resources::RenderSettings>(resources::RenderSettings{});

  // LOD first: culling tests the bounds of whichever mesh LOD selected.
  world.registerSystem<LodSystem>(ecs::SystemGroup::Presentation);
  world.registerSystem<CullingSystem>(ecs::SystemGroup::Presentation);
  world.addSystemDependencies<CullingSystem, LodSystem>();
  world.registerSystem<BatchingSystem>(ecs::SystemGroup::Presentation);
  world.addSystemDependencies<BatchingSystem, CullingSystem>();
  world.registerSystem<RenderSystem>(ecs::SystemGroup::Presentation);
  world.addSystemDependencies<RenderSystem, BatchingSystem>();
}

void RenderPlugin::ready(ecs::World &world) {
  world.setResource<renderer::AssetManager>(
      renderer::AssetManager(world.getRenderDevice()));
  auto &assetManager = world.getResource<renderer::AssetManager>();

  LOG_INFO("Loading built-in assets...");

  assetManager.loadShader(
      utils::Utils::getAssetPath("assets/shaders/diffuse.vert").c_str(),
      utils::Utils::getAssetPath("assets/shaders/diffuse.frag").c_str(),
      nullptr, "default");

  assetManager.loadTexture(
      utils::Utils::getAssetPath("assets/textures/white.png").c_str(), false,
      "white");

  assetManager.registerMaterial<renderer::DiffuseMaterial>("default_diffuse",
                                                           "default", "white");

  // Same shader as default_diffuse, but blended and without depth writes so
  // things behind it show through. The entity's ColorComponent alpha sets the
  // opacity. Back faces are culled: a closed shape shows exactly one face to
  // the camera, so each pixel is blended once instead of twice. A single
  // two-sided plane would need CullMode::None instead.
  assetManager.registerMaterial<renderer::DiffuseMaterial>(
      "transparent_diffuse", "default", "white");
  {
    renderer::api::RenderState state;
    state.cull = renderer::api::CullMode::Back;
    state.depthWrite = false;
    state.blend = renderer::api::BlendMode::Alpha;
    auto &transparent = assetManager.getMaterial(
        assetManager.getMaterialID("transparent_diffuse"));
    transparent.setType(renderer::MaterialType::Transparent);
    transparent.setRenderState(state);
  }

  assetManager.loadShader(
      utils::Utils::getAssetPath("assets/shaders/pbr.vert").c_str(),
      utils::Utils::getAssetPath("assets/shaders/pbr.frag").c_str(), nullptr,
      "pbr");

  assetManager.loadShader(
      utils::Utils::getAssetPath("assets/shaders/skybox_procedural.vert")
          .c_str(),
      utils::Utils::getAssetPath("assets/shaders/skybox_procedural.frag")
          .c_str(),
      nullptr, "skybox");

  assetManager.loadShader(
      utils::Utils::getAssetPath("assets/shaders/post_process.vert").c_str(),
      utils::Utils::getAssetPath("assets/shaders/post_process.frag").c_str(),
      nullptr, "post_process");

  assetManager.loadMesh(
      utils::Utils::getAssetPath("assets/models/cube.obj").c_str(), "cube");
  assetManager.loadMesh(
      utils::Utils::getAssetPath("assets/models/sphere_smooth.obj").c_str(),
      "sphere");

  LOG_INFO("Finished loading built-in assets");
}

void RenderPlugin::cleanup(ecs::World &world) {
  world.getResource<AssetManager>().clear();
}

} // namespace componeng::renderer
