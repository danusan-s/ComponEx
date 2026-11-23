#include "componeng/camera/camera_plugin.hpp"

#include "componeng/camera/camera_component.hpp"
#include "componeng/camera/camera_system.hpp"
#include "componeng/camera/main_camera.hpp"
#include "componeng/core/engine.hpp"
#include "componeng/core/transform_component.hpp"

namespace componeng::camera {

void CameraPlugin::build(core::Engine &engine) {
  ecs::World &world = engine.world();
  world.registerComponents<core::TransformComponent, CameraComponent>();
  world.registerSystem<CameraSystem>(ecs::SystemGroup::Simulation);
}

void CameraPlugin::ready(ecs::World &world) {
  if (world.hasResource<MainCamera>()) {
    return;
  }
  ecs::EntityID cameraEntity = world.createEntity();
  world.addComponents(
      cameraEntity,
      core::TransformComponent{.position = core::Vec3(0.0f, 5.0f, 0.0f),
                               .rotation = core::Vec3(0.0f, 0.0f, 0.0f),
                               .scale = core::Vec3(1.0f)},
      CameraComponent{.fov = 45.0f,
                      .aspectRatio = 16.0f / 9.0f,
                      .nearPlane = 0.1f,
                      .farPlane = 10000.0f});
  world.setResource(MainCamera{.entity = cameraEntity});
}

} // namespace componeng::camera
