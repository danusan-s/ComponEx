#include "componeng/core/engine.hpp"
#include "componeng/core/game.hpp"

#include "componeng/components/material_component.hpp"
#include "componeng/components/mesh_component.hpp"
#include "componeng/components/rigidbody_component.hpp"
#include "componeng/components/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/resources/main_camera.hpp"
#include "componeng/utils/utils.hpp"

#include "aim_system.hpp"
#include "helper.hpp"
#include "orb_component.hpp"
#include "player_controller.hpp"

using namespace componeng::core;
using namespace componeng::components;
using namespace componeng::ecs;
using namespace componeng::renderer;
using namespace componeng::resources;
using namespace componeng::utils;

constexpr int kOrbCount = 15;
constexpr float kFloorSize = 2.0f;

constexpr Vec3 kCameraPos = Vec3(0, 2, 0);
constexpr Vec3 kCameraRot = Vec3(0, -90, 0);
constexpr Vec3 kFloorColor = Vec3(0.5f, 0.5f, 0.5f);
constexpr Vec3 kOrbColor = Vec3(0.0f, 1.0f, 1.0f);

class AimTrain : public IGame {
public:
  void init(World &world) override {
    world.registerComponent<aim_train::OrbComponent>();
    world.registerSystem<aim_train::PlayerController>(SystemGroup::Simulation);
    world.registerSystem<aim_train::AimSystem>(SystemGroup::Simulation);

    auto &assetManager = world.get_resource<AssetManager>();
    assetManager.loadAudio(Utils::getAssetPath("assets/audio/boop.wav").c_str(),
                           "boop");
    assetManager.loadAudio(Utils::getAssetPath("assets/audio/gun.wav").c_str(),
                           "gunshot");

    auto &mainCam = world.get_resource<MainCamera>();
    auto &t = world.getComponent<TransformComponent>(mainCam.entity);
    t.position = kCameraPos;
    t.rotation = kCameraRot;

    // Spawn floor at origin
    EntityID floor = world.createEntity();
    world.addComponents(
        floor, MeshComponent{.meshName = "cube"},
        TransformComponent{.position = Vec3(0, -0.1f, 0),
                           .scale = Vec3(kFloorSize, 0.2f, kFloorSize)},
        MaterialComponent{.color = kFloorColor,
                          .textureName = "white",
                          .shaderName = "default"},
        RigidBodyComponent{.type = RigidBodyComponent::Type::Static,
                           .velocity = Vec3(0, 0, 0),
                           .mass = 0.0f,
                           .restitution = 0.0f},
        ColliderComponent{.type = ColliderType::Box});

    // Create 4 invisible walls around the floor to prevent players from falling
    EntityID wall1 = world.createEntity();
    world.addComponents(
        wall1, MeshComponent{.meshName = "cube"},
        TransformComponent{.position = Vec3(0, 1.0f, -kFloorSize / 2.0f),
                           .scale = Vec3(kFloorSize, 2.0f, 0.01f)},
        RigidBodyComponent{.type = RigidBodyComponent::Type::Static,
                           .velocity = Vec3(0, 0, 0),
                           .mass = 0.0f,
                           .restitution = 0.0f},
        ColliderComponent{.type = ColliderType::Box});

    EntityID wall2 = world.createEntity();
    world.addComponents(
        wall2, MeshComponent{.meshName = "cube"},
        TransformComponent{.position = Vec3(0, 1.0f, kFloorSize / 2.0f),
                           .scale = Vec3(kFloorSize, 2.0f, 0.01f)},
        RigidBodyComponent{.type = RigidBodyComponent::Type::Static,
                           .velocity = Vec3(0, 0, 0),
                           .mass = 0.0f,
                           .restitution = 0.0f},
        ColliderComponent{.type = ColliderType::Box});

    EntityID wall3 = world.createEntity();
    world.addComponents(
        wall3, MeshComponent{.meshName = "cube"},
        TransformComponent{.position = Vec3(-kFloorSize / 2.0f, 1.0f, 0),
                           .scale = Vec3(0.01f, 2.0f, kFloorSize)},
        RigidBodyComponent{.type = RigidBodyComponent::Type::Static,
                           .velocity = Vec3(0, 0, 0),
                           .mass = 0.0f,
                           .restitution = 0.0f},
        ColliderComponent{.type = ColliderType::Box});

    EntityID wall4 = world.createEntity();
    world.addComponents(
        wall4, MeshComponent{.meshName = "cube"},
        TransformComponent{.position = Vec3(kFloorSize / 2.0f, 1.0f, 0),
                           .scale = Vec3(0.01f, 2.0f, kFloorSize)},
        RigidBodyComponent{.type = RigidBodyComponent::Type::Static,
                           .velocity = Vec3(0, 0, 0),
                           .mass = 0.0f,
                           .restitution = 0.0f},
        ColliderComponent{.type = ColliderType::Box});

    for (int i = 0; i < kOrbCount; ++i) {
      spawnOrb(world);
    }
  }

  void shutdown(World &world) override {
  }
};

int main() {
  Engine &engine = Engine::get();
  engine.init();

  AimTrain game;
  engine.run(game);

  engine.shutdown();
  return 0;
}
