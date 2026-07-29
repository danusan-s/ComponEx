#include "componeng/core/engine.hpp"
#include "componeng/core/game.hpp"

#include "componeng/components/material_component.hpp"
#include "componeng/components/mesh_component.hpp"
#include "componeng/components/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/resources/main_camera.hpp"

#include "camera_controller.hpp"
#include "gravity_system.hpp"
#include "planet_component.hpp"

using namespace componeng::core;
using namespace componeng::components;
using namespace componeng::ecs;
using namespace componeng::utils;
using namespace planetary_sim;

class PlanetarySim : public IGame {
public:
  void init(World &world) override {
    world.registerComponent<planetary_sim::PlanetComponent>();
    world.registerComponent<planetary_sim::StarComponent>();

    world.registerSystem<planetary_sim::GravitySystem>(SystemGroup::Simulation);
    world.registerSystem<planetary_sim::CameraController>(
        SystemGroup::Simulation);

    auto &mainCam = world.get_resource<componeng::resources::MainCamera>();
    auto &camT = world.getComponent<TransformComponent>(mainCam.entity);
    camT.position = Vec3(0, 3, 15);
    camT.rotation = Vec3(-10, -90, 0);

    EntityID star = world.createEntity();
    world.addComponents(
        star, StarComponent{.mass = 1000.0f},
        MeshComponent{.meshName = "sphere"},
        TransformComponent{.position = Vec3(0, 0, 0), .scale = Vec3(2.0f)},
        MaterialComponent{.color = Vec3(1.0f, 0.8f, 0.2f),
                          .textureName = "white",
                          .shaderName = "default"});

    const int kPlanetCount = 5;
    for (int i = 0; i < kPlanetCount; ++i) {
      EntityID planet = world.createEntity();
      float radius = 4.0f + i * 3.0f;
      float speed = 1.0f / (radius * 0.5f);
      float phase = static_cast<float>(i) * 1.256f;
      float scale = 0.3f + i * 0.1f;

      world.addComponents(
          planet,
          PlanetComponent{.mass = 1.0f,
                          .orbitRadius = radius,
                          .orbitSpeed = speed,
                          .phase = phase,
                          .rotationSpeed = 0.5f},
          MeshComponent{.meshName = "sphere"},
          TransformComponent{.position = Vec3(radius, 0, 0),
                             .scale = Vec3(scale)},
          MaterialComponent{.color = Vec3(0.2f, 0.4f + i * 0.1f, 0.8f),
                            .textureName = "white",
                            .shaderName = "default"});
    }
  }

  void shutdown(World &world) override {
  }
};

int main() {
  Engine &engine = Engine::get();
  engine.init();

  PlanetarySim game;
  engine.run(game);

  engine.shutdown();
  return 0;
}
