#include "collision_system.hpp"
#include "celestial_component.hpp"
#include "componeng/audio/audio_manager.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/events/collision_event.hpp"
#include "componeng/physics/collider_component.hpp"
#include "componeng/physics/rigidbody_component.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>

using namespace componeng::renderer;

namespace {

// Spawns a burst of tiny cubes at the impact point, spewing sideways
// (mostly perpendicular to collision normal) to visualize debris.
void spawnImpactParticles(componeng::ecs::World &world,
                          componeng::core::Vec3 impactPos,
                          componeng::core::Vec3 normal,
                          componeng::core::Vec3 baseVelocity,
                          componeng::core::Vec3 debrisColor) {
  using namespace componeng::core;
  using namespace componeng::physics;
  using namespace componeng::renderer;
  using namespace componeng::ecs;

  float nLen = length(normal);
  if (nLen < 1e-5f)
    return;
  normal = normal / nLen;

  Vec3 up = std::abs(normal.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
  Vec3 tangent = normalize(cross(normal, up));
  Vec3 bitangent = normalize(cross(normal, tangent));

  thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159265f);
  std::uniform_real_distribution<float> speedDist(3.0f, 9.0f);
  std::uniform_real_distribution<float> scaleDist(0.02f, 0.06f);
  std::uniform_real_distribution<float> normalBiasDist(-0.1f, 0.3f);
  std::uniform_real_distribution<float> colorJitter(-0.08f, 0.08f);

  constexpr int kParticleCount = 8;

  for (int i = 0; i < kParticleCount; ++i) {
    float angle = angleDist(rng);
    float speed = speedDist(rng);
    float scale = scaleDist(rng);

    Vec3 dir = std::cos(angle) * tangent + std::sin(angle) * bitangent;
    dir = dir + normal * normalBiasDist(rng);
    dir = normalize(dir);

    Vec3 vel = baseVelocity + dir * speed;
    Vec3 pos = impactPos + dir * (scale * 1.5f + 0.05f);

    Vec3 col = debrisColor;
    col.x = std::clamp(col.x + colorJitter(rng), 0.0f, 1.0f);
    col.y = std::clamp(col.y + colorJitter(rng), 0.0f, 1.0f);
    col.z = std::clamp(col.z + colorJitter(rng), 0.0f, 1.0f);

    EntityID e = world.createEntity();
    // No ColliderComponent - particles are visual debris only and should
    // not generate collision events or get consumed by planets.
    world.addComponents(
        e, TransformComponent{.position = pos, .scale = Vec3(scale)},
        MeshComponent{.meshName = "cube"},
        MaterialComponent{.materialName = "default_diffuse"},
        ColorComponent{.color = Vec4(col, 1.0f)},
        RigidBodyComponent{.type = RigidBodyComponent::Type::Dynamic,
                           .velocity = vel,
                           .mass = 0.02f,
                           .restitution = 0.4f});
  }
}

} // namespace

namespace planetary_sim {

void CollisionSystem::onUpdate(const componeng::ecs::SystemState &state) {
  auto &collisionEvents =
      state.world->getEvents<componeng::events::CollisionEvent>();
  auto &assetManager =
      state.world->getResource<componeng::renderer::AssetManager>();
  auto &audioManager =
      state.world->getResource<componeng::audio::AudioManager>();

  std::unordered_set<componeng::ecs::EntityID> destroyed;

  for (const auto &event : collisionEvents) {
    if (destroyed.count(event.entityA) || destroyed.count(event.entityB))
      continue;

    if (!state.world->hasComponent<componeng::core::TransformComponent>(
            event.entityA) ||
        !state.world->hasComponent<componeng::core::TransformComponent>(
            event.entityB) ||
        !state.world->hasComponent<componeng::physics::RigidBodyComponent>(
            event.entityA) ||
        !state.world->hasComponent<componeng::physics::RigidBodyComponent>(
            event.entityB))
      continue;

    // Only celestial bodies should merge/spawn debris - particles have no
    // CelestialComponent
    if (!state.world->hasComponent<planetary_sim::CelestialComponent>(
            event.entityA) ||
        !state.world->hasComponent<planetary_sim::CelestialComponent>(
            event.entityB))
      continue;

    // Compute impact point for sound + particles
    auto &a_transform =
        state.world->getComponent<componeng::core::TransformComponent>(
            event.entityA);
    auto impactPos = a_transform.position -
                     event.info.normal * event.info.penetration * 0.5f;

    // Play collision sound
    {
      auto soundId = audioManager.createSound("boop");
      audioManager.getSound(soundId)
          .setPosition(impactPos.x, impactPos.y, impactPos.z)
          .setVolume(0.8f)
          .setPitch(1.0f)
          .setLooping(false)
          .set3D(1.0f, 1000.0f)
          .play();
    }

    auto &transformA =
        state.world->getComponent<componeng::core::TransformComponent>(
            event.entityA);
    auto &transformB =
        state.world->getComponent<componeng::core::TransformComponent>(
            event.entityB);
    auto &rbA =
        state.world->getComponent<componeng::physics::RigidBodyComponent>(
            event.entityA);
    auto &rbB =
        state.world->getComponent<componeng::physics::RigidBodyComponent>(
            event.entityB);

    componeng::ecs::EntityID survivor;
    componeng::ecs::EntityID victim;
    if (rbA.mass > rbB.mass) {
      survivor = event.entityA;
      victim = event.entityB;
    } else if (rbB.mass > rbA.mass) {
      survivor = event.entityB;
      victim = event.entityA;
    } else {
      float rA = transformA.scale.x;
      float rB = transformB.scale.x;
      if (rA >= rB) {
        survivor = event.entityA;
        victim = event.entityB;
      } else {
        survivor = event.entityB;
        victim = event.entityA;
      }
    }

    if (survivor == victim)
      continue;
    if (destroyed.count(survivor) || destroyed.count(victim))
      continue;

    auto &survivorTransform =
        state.world->getComponent<componeng::core::TransformComponent>(
            survivor);
    auto &victimTransform =
        state.world->getComponent<componeng::core::TransformComponent>(victim);
    auto &survivorRb =
        state.world->getComponent<componeng::physics::RigidBodyComponent>(
            survivor);
    auto &victimRb =
        state.world->getComponent<componeng::physics::RigidBodyComponent>(
            victim);

    float survivorRadius = survivorTransform.scale.x;
    float victimRadius = victimTransform.scale.x;

    float newRadius =
        std::cbrt(survivorRadius * survivorRadius * survivorRadius +
                  victimRadius * victimRadius * victimRadius);

    float newMass = survivorRb.mass + victimRb.mass;

    componeng::core::Vec3 newVelocity = (survivorRb.velocity * survivorRb.mass +
                                         victimRb.velocity * victimRb.mass) /
                                        newMass;

    // Determine debris color (victim's color if available, else survivor)
    componeng::core::Vec3 debrisColor(0.6f, 0.6f, 0.6f);
    if (state.world->hasComponent<componeng::renderer::ColorComponent>(
            victim)) {
      auto &col =
          state.world->getComponent<componeng::renderer::ColorComponent>(
              victim);
      debrisColor =
          componeng::core::Vec3(col.color.x, col.color.y, col.color.z);
    } else if (state.world->hasComponent<componeng::renderer::ColorComponent>(
                   survivor)) {
      auto &col =
          state.world->getComponent<componeng::renderer::ColorComponent>(
              survivor);
      debrisColor =
          componeng::core::Vec3(col.color.x, col.color.y, col.color.z);
    }

    // Spawn debris particles sideways from collision point
    componeng::core::Vec3 baseVel = newVelocity;
    spawnImpactParticles(*state.world, impactPos, event.info.normal, baseVel,
                         debrisColor);

    survivorTransform.scale = componeng::core::Vec3(newRadius);
    survivorRb.mass = newMass;
    survivorRb.velocity = newVelocity;

    state.world->destroyEntity(victim);
    destroyed.insert(victim);
  }
}

} // namespace planetary_sim
