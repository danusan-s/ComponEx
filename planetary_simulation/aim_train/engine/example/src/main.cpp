#include "componeng/audio/audio_component.hpp"
#include "componeng/audio/audio_manager.hpp"
#include "componeng/camera/main_camera.hpp"
#include "componeng/core/default_plugins.hpp"
#include "componeng/core/engine.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/scene_serializer.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/physics/collider_component.hpp"
#include "componeng/physics/rigidbody_component.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/utils/utils.hpp"
#include "orbiting_sound.hpp"
#include "player_controller.hpp"

#include <random>

using namespace componeng::core;
using namespace componeng::audio;
using namespace componeng::physics;
using namespace componeng::ecs;
using namespace componeng::renderer;
using namespace componeng::utils;
using namespace componeng::camera;

bool loadFromFile = false;
bool saveToFile = true;

class Game : public IPlugin {
public:
  void build(Engine &engine) override {
    engine.world().registerSystem<PlayerController>(SystemGroup::Simulation);
    engine.world().registerSystem<OrbitingSound>(SystemGroup::Simulation);
  }

  void ready(World &world) override {
    constexpr float positionRange = 2000.0f;
    // Walls are a little taller than the arena so they overlap floor and roof
    constexpr float wallHeight = positionRange * 1.1f;
    constexpr float scaleMin = 1.0f;
    constexpr float scaleMax = 5.0f;
    constexpr float massMin = 0.5f;
    constexpr float massMax = 5.0f;
    constexpr float velocityRange = 50.0f;
    constexpr int count = 10000; // count each for cube and sphere

    auto &audioManager = world.getResource<AudioManager>();
    audioManager.registerSound(
        Utils::getAssetPath("assets/audio/boop.wav").c_str(), "boop");

    if (loadFromFile) {
      SceneSerializer::load(
          world, Utils::getAssetPath("assets/scenes/test_scene.json"));
      return;
    }
    std::default_random_engine generator{static_cast<unsigned int>(
        std::chrono::system_clock::now().time_since_epoch().count())};
    std::uniform_real_distribution<float> randPosition(-positionRange,
                                                       positionRange);
    std::uniform_real_distribution<float> randScale(scaleMin, scaleMax);
    std::uniform_real_distribution<float> randColor(0.0f, 1.0f);
    std::uniform_real_distribution<float> randMass(massMin, massMax);
    std::uniform_real_distribution<float> randVelocity(-velocityRange,
                                                       velocityRange);

    for (int i = 0; i < count; ++i) {
      EntityID entity = world.createEntity();
      float scale = randScale(generator);

      world.addComponents(
          entity, componeng::renderer::MeshComponent{.meshName = "cube"},
          componeng::core::TransformComponent{
              .position = Vec3(randPosition(generator), randPosition(generator),
                               randPosition(generator)),
              .rotation = Vec3(0.0f),
              .scale = Vec3(scale)},
          RigidBodyComponent{.type = RigidBodyComponent::Type::Dynamic,
                             .velocity = Vec3(randVelocity(generator),
                                              randVelocity(generator),
                                              randVelocity(generator)),
                             .mass = randMass(generator),
                             .restitution = 1.0f},
          MaterialComponent{.materialName = "default_diffuse"},
          ColorComponent{.color =
                             Vec4(randColor(generator), randColor(generator),
                                  randColor(generator), 1.0f)},
          ColliderComponent{.type = ColliderType::Box,
                            .transform =
                                TransformComponent{.position = Vec3(0.0f),
                                                   .rotation = Vec3(0.0f),
                                                   .scale = Vec3(1.0f)}});

      entity = world.createEntity();
      scale = randScale(generator);

      world.addComponents(
          entity, MeshComponent{.meshName = "sphere"},
          TransformComponent{.position = Vec3(randPosition(generator),
                                              randPosition(generator),
                                              randPosition(generator)),
                             .rotation = Vec3(0.0f),
                             .scale = Vec3(scale)},
          RigidBodyComponent{.type = RigidBodyComponent::Type::Dynamic,
                             .velocity = Vec3(randVelocity(generator),
                                              randVelocity(generator),
                                              randVelocity(generator)),
                             .mass = randMass(generator),
                             .restitution = 1.0f},
          MaterialComponent{.materialName = "default_diffuse"},
          ColorComponent{.color =
                             Vec4(randColor(generator), randColor(generator),
                                  randColor(generator), 1.0f)},
          ColliderComponent{.type = ColliderType::Sphere,
                            .transform =
                                TransformComponent{.position = Vec3(0.0f),
                                                   .rotation = Vec3(0.0f),
                                                   .scale = Vec3(1.0f)}});
    }

    EntityID floor = world.createEntity();
    world.addComponents(
        floor,
        TransformComponent{.position = Vec3(0.0f, -positionRange, 0.0f),
                           .rotation = Vec3(0.0f),
                           .scale = Vec3(positionRange, 1.0f, positionRange)},
        MeshComponent{.meshName = "cube"},
        MaterialComponent{.materialName = "default_diffuse"},
        ColorComponent{.color = Vec4(0.5f, 0.5f, 0.5f, 1.0f)},
        RigidBodyComponent{.type = RigidBodyComponent::Type::Static,
                           .restitution = 1.0f},
        ColliderComponent{.type = ColliderType::Box,
                          .transform =
                              TransformComponent{.position = Vec3(0.0f),
                                                 .rotation = Vec3(0.0f),
                                                 .scale = Vec3(1.0f)}});

    // Four walls enclosing the play area (scale = half extents)
    struct WallSpec {
      Vec3 position;
      Vec3 scale;
    };
    // The x walls run one unit longer so the corners close.
    const WallSpec walls[4] = {
        {Vec3(-positionRange, 0.0f, 0.0f),
         Vec3(1.0f, wallHeight, positionRange + 1.0f)},
        {Vec3(positionRange, 0.0f, 0.0f),
         Vec3(1.0f, wallHeight, positionRange + 1.0f)},
        {Vec3(0.0f, 0.0f, -positionRange),
         Vec3(positionRange - 1.0f, wallHeight, 1.0f)},
        {Vec3(0.0f, 0.0f, positionRange),
         Vec3(positionRange - 1.0f, wallHeight, 1.0f)},
    };
    for (const WallSpec &spec : walls) {
      EntityID wall = world.createEntity();
      world.addComponents(
          wall,
          TransformComponent{.position = spec.position,
                             .rotation = Vec3(0.0f),
                             .scale = spec.scale},
          MeshComponent{.meshName = "cube"},
          MaterialComponent{.materialName = "transparent_diffuse"},
          ColorComponent{.color = Vec4(0.5f, 0.5f, 0.5f, 0.3f)},
          RigidBodyComponent{.type = RigidBodyComponent::Type::Static,
                             .restitution = 1.0f},
          ColliderComponent{.type = ColliderType::Box,
                            .transform =
                                TransformComponent{.position = Vec3(0.0f),
                                                   .rotation = Vec3(0.0f),
                                                   .scale = Vec3(1.0f)}});
    }

    EntityID roof = world.createEntity();
    world.addComponents(
        roof,
        TransformComponent{.position = Vec3(0.0f, positionRange, 0.0f),
                           .rotation = Vec3(0.0f),
                           .scale = Vec3(positionRange, 1.0f, positionRange)},
        MeshComponent{.meshName = "cube"},
        MaterialComponent{.materialName = "default_diffuse"},
        ColorComponent{.color = Vec4(0.5f, 0.5f, 0.5f, 1.0f)},
        RigidBodyComponent{.type = RigidBodyComponent::Type::Static,
                           .restitution = 1.0f},
        ColliderComponent{.type = ColliderType::Box,
                          .transform =
                              TransformComponent{.position = Vec3(0.0f),
                                                 .rotation = Vec3(0.0f),
                                                 .scale = Vec3(1.0f)}});

    EntityID audioEntity = world.createEntity();
    world.addComponents(
        audioEntity,
        TransformComponent{.position = Vec3(0.0f, 0.0f, 0.0f),
                           .rotation = Vec3(0.0f),
                           .scale = Vec3(1.0f)},
        MaterialComponent{.materialName = "default_diffuse"},
        MeshComponent{.meshName = "cube"},
        ColorComponent{.color = Vec4(1.0f, 1.0f, 1.0f, 1.0f)},
        ColliderComponent{.type = ColliderType::Box,
                          .transform =
                              TransformComponent{.position = Vec3(0.0f),
                                                 .rotation = Vec3(0.0f),
                                                 .scale = Vec3(1.0f)}},
        AudioComponent{.audioName = "boop",
                       .loop = true,
                       .isPlaying = false,
                       .is3D = true,
                       .minDistance = 1.0f,
                       .maxDistance = 100.0f});

    auto &camera = world.getResource<MainCamera>();
    EntityID cameraEntity = camera.entity;
    TransformComponent &playerPos =
        world.getComponent<TransformComponent>(cameraEntity);
    playerPos.position = Vec3(-1.5 * positionRange, -0.5 * positionRange, 0.0f);
    playerPos.rotation = Vec3(0.0f, 0.0f, 0.0f);
  }

  void cleanup(World &world) override {
    if (saveToFile) {
      SceneSerializer::save(
          world, Utils::getAssetPath("assets/scenes/test_scene.json"));
    }
  }
};

int main() {
  Engine engine;
  engine.add(DefaultPlugins{}).add(Game{}).run();
  return 0;
}
