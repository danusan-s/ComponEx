#include "componeng/core/engine.hpp"
#include "componeng/core/game.hpp"

#include "componeng/components/collider_component.hpp"
#include "componeng/components/material_component.hpp"
#include "componeng/components/mesh_component.hpp"
#include "componeng/components/rigidbody_component.hpp"
#include "componeng/components/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/physics/physics_system.hpp"
#include "componeng/resources/main_camera.hpp"
#include "componeng/resources/physics_config.hpp"
#include "componeng/utils/utils.hpp"

#include "camera_controller.hpp"
#include "celestial_component.hpp"
#include "collision_system.hpp"
#include "gravity_system.hpp"

#include <random>

using namespace componeng::core;
using namespace componeng::renderer;
using namespace componeng::components;
using namespace componeng::ecs;
using namespace componeng::utils;
using namespace componeng::physics;
using namespace planetary_sim;

constexpr int kBodyCount = 10;
constexpr float kPositionRange = 20.0f;
constexpr float kMassMin = 1.0f;
constexpr float kMassMax = 500.0f;
constexpr float kSpeedScale = 2.0f;
constexpr float kScaleMin = 0.15f;
constexpr float kScaleMax = 1.0f;

static std::mt19937 &rng() {
  static std::mt19937 gen(std::random_device{}());
  return gen;
}

static Vec3 randomDirection() {
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  Vec3 v(dist(rng()), dist(rng()), dist(rng()));
  return normalize(v);
}

static Vec3 randomColor() {
  std::uniform_real_distribution<float> hue(0.0f, 1.0f);
  std::uniform_real_distribution<float> sat(0.4f, 1.0f);
  std::uniform_real_distribution<float> val(0.5f, 1.0f);

  float h = hue(rng());
  float s = sat(rng());
  float v = val(rng());

  int i = int(h * 6.0f);
  float f = h * 6.0f - i;
  float p = v * (1.0f - s);
  float q = v * (1.0f - f * s);
  float t = v * (1.0f - (1.0f - f) * s);

  switch (i % 6) {
    case 0:
      return Vec3(v, t, p);
    case 1:
      return Vec3(q, v, p);
    case 2:
      return Vec3(p, v, t);
    case 3:
      return Vec3(p, q, v);
    case 4:
      return Vec3(t, p, v);
    default:
      return Vec3(v, p, q);
  }
}

static void generateRandomBody(World &world) {
  std::uniform_real_distribution<float> posDist(-kPositionRange,
                                                kPositionRange);
  std::uniform_real_distribution<float> massDist(kMassMin, kMassMax);
  std::uniform_real_distribution<float> scaleDist(kScaleMin, kScaleMax);
  std::uniform_real_distribution<float> speedDist(0.5f, 1.5f);
  std::uniform_real_distribution<float> rotDist(-2.0f, 2.0f);

  Vec3 position(posDist(rng()), posDist(rng()), posDist(rng()));
  float dist = length(position);
  if (dist < 5.0f)
    position = normalize(position) * 5.0f;

  float mass = massDist(rng());
  float scale = scaleDist(rng());
  Vec3 color = randomColor();

  Vec3 up;
  if (std::abs(position.y) < std::abs(position.x))
    up = Vec3(0, 1, 0);
  else
    up = Vec3(1, 0, 0);
  Vec3 tangent = normalize(cross(position, up));
  float speed = speedDist(rng()) * kSpeedScale / std::sqrt(dist);
  Vec3 velocity = tangent * speed;

  EntityID entity = world.createEntity();
  world.addComponents(
      entity, CelestialComponent{.rotationSpeed = rotDist(rng())},
      RigidBodyComponent{.type = RigidBodyComponent::Type::Dynamic,
                         .velocity = velocity,
                         .mass = mass,
                         .restitution = 1.0f},
      ColliderComponent{.type = ColliderType::Sphere},
      MeshComponent{.meshName = "sphere"},
      TransformComponent{.position = position, .scale = Vec3(scale)},
      MaterialComponent{
          .color = color, .textureName = "white", .shaderName = "default"});
}

class PlanetarySim : public IGame {
public:
  void init(World &world) override {
    world.registerComponent<planetary_sim::CelestialComponent>();

    world.registerSystem<planetary_sim::GravitySystem>(SystemGroup::Simulation);
    world.registerSystem<planetary_sim::CameraController>(
        SystemGroup::Simulation);
    world.registerSystem<planetary_sim::CollisionSystem>(
        SystemGroup::Simulation);
    world
        .addSystemDependencies<planetary_sim::CollisionSystem, PhysicsSystem>();

    auto &assetManager = world.get_resource<AssetManager>();
    assetManager.loadAudio(Utils::getAssetPath("assets/audio/boop.wav").c_str(),
                           "boop");

    auto &mainCam = world.get_resource<componeng::resources::MainCamera>();
    auto &camT = world.getComponent<TransformComponent>(mainCam.entity);
    camT.position = Vec3(0, 3, 15);
    camT.rotation = Vec3(-10, -90, 0);

    auto &physicsConfig =
        world.get_resource<componeng::resources::PhysicsConfig>();
    physicsConfig.gravity = Vec3(0, 0, 0);

    EntityID star = world.createEntity();
    world.addComponents(
        star, CelestialComponent{},
        RigidBodyComponent{.type = RigidBodyComponent::Type::Dynamic,
                           .mass = 10000.0f,
                           .restitution = 1.0f},
        MeshComponent{.meshName = "sphere"},
        TransformComponent{.position = Vec3(0, 0, 0), .scale = Vec3(2.0f)},
        ColliderComponent{.type = ColliderType::Sphere},
        MaterialComponent{.color = Vec3(1.0f, 0.8f, 0.2f),
                          .textureName = "white",
                          .shaderName = "default"});

    for (int i = 0; i < kBodyCount; ++i) {
      generateRandomBody(world);
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
