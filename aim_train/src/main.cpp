#include "componeng/core/engine.hpp"
#include "componeng/core/game.hpp"

#include "componeng/audio/audio_manager.hpp"
#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/physics/collider_component.hpp"
#include "componeng/physics/rigidbody_component.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/utils/utils.hpp"

#include "aim_system.hpp"
#include "helper.hpp"
#include "orb_component.hpp"
#include "player_controller.hpp"

using namespace componeng::audio;
using namespace componeng::camera;
using namespace componeng::core;
using namespace componeng::ecs;
using namespace componeng::input;
using namespace componeng::physics;
using namespace componeng::renderer;
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

    auto &audioManager = world.getResource<AudioManager>();

    audioManager.registerSound(
        Utils::getAssetPath("assets/audio/boop.wav").c_str(), "boop");
    audioManager.registerSound(
        Utils::getAssetPath("assets/audio/gun.wav").c_str(), "gunshot");

    auto &mainCam = world.getResource<MainCamera>();
    auto &t = world.getComponent<TransformComponent>(mainCam.entity);
    t.position = kCameraPos;
    t.rotation = kCameraRot;

    // Spawn floor at origin
    EntityID floor = world.createEntity();
    world.addComponents(
        floor, MeshComponent{.meshName = "cube"},
        TransformComponent{.position = Vec3(0, -0.1f, 0),
                           .scale = Vec3(kFloorSize, 0.2f, kFloorSize)},
        MaterialComponent{.materialName = "default_diffuse"},
        ColorComponent{.color = Vec4(kFloorColor, 1.0f)},
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
