#pragma once

#include "fake_render_device.hpp"

#include "componeng/camera/camera_component.hpp"
#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset/diffuse.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/batching/batching_system.hpp"
#include "componeng/renderer/component/light_component.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/renderer/component/skybox_component.hpp"
#include "componeng/renderer/culling/culling_system.hpp"
#include "componeng/renderer/frame_data.hpp"
#include "componeng/renderer/render_system.hpp"
#include "componeng/resources/fog.hpp"
#include "componeng/resources/render_settings.hpp"
#include "componeng/utils/utils.hpp"

#include <functional>
#include <memory>

// A World wired to the fake render device with the batching and render
// systems (and optionally culling) registered, plus a handful of assets.
namespace rscene {

using namespace componeng;
using namespace componeng::core;
using namespace componeng::ecs;
using namespace componeng::renderer;

// instanceModel (mat4 as 4 rows) + instanceColor (vec4): what diffuse.vert
// reflects to.
inline api::VertexLayout diffuseLayout() {
  api::VertexLayout layout;
  for (int row = 0; row < 4; ++row) {
    layout.attributes.push_back(
        {Name("instanceModel"), uint32_t(row * 16), 4, false, row});
  }
  layout.attributes.push_back({Name("instanceColor"), 64, 4, false, -1});
  layout.stride = 80;
  return layout;
}

// A tiny triangle (3 indices) and quad (6 indices) so the two meshes are
// distinguishable by index count.
inline const char *kTriangleObj =
    "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nvt 0 0\nf 1/1/1 2/1/1 3/1/1\n";
inline const char *kQuadObj =
    "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\nvt 0 0\n"
    "f 1/1/1 2/1/1 3/1/1 4/1/1\n";

struct Scene {
  std::unique_ptr<World> world;
  fake::FakeRenderDevice *device = nullptr; // owned by the world
  EntityID camera = INVALID_ENTITY;

  fake::Recorder &rec() {
    return device->rec;
  }
  AssetManager &assets() {
    return world->getResource<AssetManager>();
  }

  void frame() {
    world->updateSystems(0.016f);
  }

  EntityID spawn(const char *mesh, const char *material, float x = 0.0f) {
    EntityID e = world->createEntity();
    world->addComponents(e,
                         TransformComponent{.position = Vec3(x, 0.0f, 0.0f),
                                            .rotation = Vec3(0.0f),
                                            .scale = Vec3(1.0f)},
                         MeshComponent{.meshName = mesh, .visible = true},
                         MaterialComponent{.materialName = material});
    return e;
  }
};

// shaders: "a" = id 1, "b" = id 2.  textures: "t1" = id 1, "t2" = id 2.
// meshes: "tri" (3 indices), "quad" (6).
// materials: matA = shader a + t1, matB = shader b + t2, matC = shader a + t2.
inline std::unique_ptr<Scene>
makeScene(const std::function<void(fake::Recorder &)> &configure = {},
          bool withCulling = false) {
  auto scene = std::make_unique<Scene>();
  scene->world = std::make_unique<World>();
  World &world = *scene->world;

  auto device = std::make_unique<fake::FakeRenderDevice>();
  scene->device = device.get();
  scene->device->rec.instanceLayout = diffuseLayout();
  if (configure) {
    configure(scene->device->rec);
  }
  std::unique_ptr<api::IRenderDevice> base = std::move(device);
  world.setResource<std::unique_ptr<api::IRenderDevice>>(std::move(base));
  world.setResource<AssetManager>(AssetManager(world.getRenderDevice()));
  world.setResource<resources::Fog>(resources::Fog{});
  // Off by default so a test sees each batch drawn once; the depth pre-pass
  // tests turn it on.
  world.setResource<resources::RenderSettings>(
      resources::RenderSettings{.depthPrepass = false});

  world.registerComponents<TransformComponent, MeshComponent, MaterialComponent,
                           ColorComponent, camera::CameraComponent,
                           DirectionalLightComponent, SkyboxComponent>();

  AssetManager &assets = scene->assets();
  // Shader sources are never read by the fake device, so missing files are
  // fine; what matters is that creation goes through the device.
  assets.loadShader("none.vert", "none.frag", nullptr, "a");
  assets.loadShader("none.vert", "none.frag", nullptr, "b");
  const std::string white =
      utils::Utils::getAssetPath("assets/textures/white.png");
  assets.loadTexture(white.c_str(), false, "t1");
  assets.loadTexture(white.c_str(), false, "t2");

  for (auto [name, obj] :
       {std::pair{"tri", kTriangleObj}, std::pair{"quad", kQuadObj}}) {
    auto mesh = std::make_unique<Mesh>();
    mesh->generateFromWavefrontObj(obj);
    assets.addMesh(name, std::move(mesh));
  }

  assets.registerMaterial<DiffuseMaterial>("matA", "a", "t1");
  assets.registerMaterial<DiffuseMaterial>("matB", "b", "t2");
  assets.registerMaterial<DiffuseMaterial>("matC", "a", "t2");

  // Camera with an easily recognised view-projection matrix.
  scene->camera = world.createEntity();
  camera::CameraComponent cam;
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      cam.viewProjectionMatrix[c][r] = float(c * 4 + r) + 0.5f;
  world.addComponents(scene->camera,
                      TransformComponent{.position = Vec3(1, 2, 3),
                                         .rotation = Vec3(0.0f),
                                         .scale = Vec3(1.0f)},
                      camera::CameraComponent(cam));
  world.setResource(camera::MainCamera{scene->camera});

  EntityID sun = world.createEntity();
  world.addComponents(
      sun, DirectionalLightComponent{.direction = Vec3(0.0f, 1.0f, 0.0f),
                                     .color = Vec3(0.5f, 0.25f, 0.125f)});

  if (withCulling) {
    world.registerSystem<CullingSystem>(SystemGroup::Presentation);
  }
  world.registerSystem<BatchingSystem>(SystemGroup::Presentation);
  if (withCulling) {
    world.addSystemDependencies<BatchingSystem, CullingSystem>();
  }
  world.registerSystem<RenderSystem>(SystemGroup::Presentation);
  world.addSystemDependencies<RenderSystem, BatchingSystem>();
  world.createSystems();
  return scene;
}

// 3 x (tri, matA), 2 x (quad, matA), 4 x (tri, matB), 1 x (tri, matC),
// deliberately interleaved.
inline void spawnMixed(Scene &s) {
  s.spawn("tri", "matB", 0);
  s.spawn("tri", "matA", 1);
  s.spawn("tri", "matC", 2);
  s.spawn("quad", "matA", 3);
  s.spawn("tri", "matB", 4);
  s.spawn("tri", "matA", 5);
  s.spawn("quad", "matA", 6);
  s.spawn("tri", "matB", 7);
  s.spawn("tri", "matA", 8);
  s.spawn("tri", "matB", 9);
}

} // namespace rscene
