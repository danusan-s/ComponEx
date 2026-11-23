#include "componeng/physics/physics_system.hpp"

#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/entity.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/events/collision_event.hpp"
#include "componeng/physics/broad_phase.hpp"
#include "componeng/physics/collider_component.hpp"
#include "componeng/physics/collision_detection.hpp"
#include "componeng/physics/physics_config.hpp"
#include "componeng/physics/rigidbody_component.hpp"

#include <algorithm>
#include <vector>

namespace componeng::physics {

struct EntityPhysicsData {
  ecs::EntityID entity;
  const core::TransformComponent *transform;
  // Non-null only for bodies that can be moved (those with a RigidBody).
  // Colliders without one are only read, so their Transform column is never
  // marked written and downstream caches keyed on its version stay valid.
  core::TransformComponent *writableTransform;
  RigidBodyComponent *rigidbody;
  const ColliderComponent *collider;
};

struct CollisionPair {
  size_t indexA;
  size_t indexB;
  CollisionInfo info;
};

static void resolveCollision(EntityPhysicsData &a, EntityPhysicsData &b,
                             const CollisionInfo &info) {
  if (a.collider->isTrigger || b.collider->isTrigger)
    return;

  float inverseMassA = 0.0f;
  float inverseMassB = 0.0f;
  float restitutionA = 0.0f;
  float restitutionB = 0.0f;

  if (a.rigidbody && a.rigidbody->type != RigidBodyComponent::Type::Static) {
    if (a.rigidbody->type == RigidBodyComponent::Type::Dynamic) {
      inverseMassA =
          a.rigidbody->mass != 0.0f ? 1.0f / a.rigidbody->mass : 0.0f;
    }
    restitutionA = a.rigidbody->restitution;
  }

  if (b.rigidbody && b.rigidbody->type != RigidBodyComponent::Type::Static) {
    if (b.rigidbody->type == RigidBodyComponent::Type::Dynamic) {
      inverseMassB =
          b.rigidbody->mass != 0.0f ? 1.0f / b.rigidbody->mass : 0.0f;
    }
    restitutionB = b.rigidbody->restitution;
  }

  if (inverseMassA == 0.0f && inverseMassB == 0.0f)
    return;

  core::Vec3 velocityA = a.rigidbody ? a.rigidbody->velocity : core::Vec3(0.0f);
  core::Vec3 velocityB = b.rigidbody ? b.rigidbody->velocity : core::Vec3(0.0f);
  core::Vec3 relativeVelocity = velocityB - velocityA;

  float velocityAlongNormal = dot(relativeVelocity, info.normal);

  if (velocityAlongNormal > 0.0f)
    return;

  float e = std::max(restitutionA, restitutionB);

  float j = -(1.0f + e) * velocityAlongNormal / (inverseMassA + inverseMassB);

  core::Vec3 impulse = j * info.normal;

  if (a.rigidbody && a.rigidbody->type == RigidBodyComponent::Type::Dynamic) {
    a.rigidbody->velocity -= impulse * inverseMassA;
  }
  if (b.rigidbody && b.rigidbody->type == RigidBodyComponent::Type::Dynamic) {
    b.rigidbody->velocity += impulse * inverseMassB;
  }

  constexpr float percent = 0.8f;
  constexpr float slop = 0.01f;
  float correctionMagnitude = std::max(info.penetration - slop, 0.0f) *
                              percent / (inverseMassA + inverseMassB);
  core::Vec3 correction = correctionMagnitude * info.normal;

  if (a.rigidbody && a.rigidbody->type == RigidBodyComponent::Type::Dynamic) {
    a.writableTransform->position -= correction * inverseMassA;
  }
  if (b.rigidbody && b.rigidbody->type == RigidBodyComponent::Type::Dynamic) {
    b.writableTransform->position += correction * inverseMassB;
  }
}

void PhysicsSystem::onUpdate(const ecs::SystemState &state) {
  m_accumulatedTime += state.deltaTime;

  const auto &physicsConfig = state.world->getResource<PhysicsConfig>();
  const float fixedTimeStep = physicsConfig.fixedTimeStep;
  core::Vec3 g_gravity = physicsConfig.gravity;

  while (m_accumulatedTime >= fixedTimeStep) {
    m_accumulatedTime -= fixedTimeStep;

    ecs::ThreadPool &pool = state.world->threadPool();
    state.world->query()
        .write<core::TransformComponent, RigidBodyComponent>()
        .eachParallel(pool, [&](core::TransformComponent &transform,
                                RigidBodyComponent &rigidbody) {
          if (rigidbody.type == RigidBodyComponent::Type::Static)
            return;
          if (rigidbody.type == RigidBodyComponent::Type::Dynamic) {
            rigidbody.velocity += g_gravity * fixedTimeStep;
          }
          transform.position += rigidbody.velocity * fixedTimeStep;
        });

    std::vector<EntityPhysicsData> colliders;

    // Transform and RigidBody are written (the resolver moves bodies and
    // changes their velocity); colliders are only inspected.
    state.world->query()
        .write<core::TransformComponent, RigidBodyComponent>()
        .read<ColliderComponent>()
        .eachWithEntity([&](ecs::EntityID entity,
                            core::TransformComponent &transform,
                            RigidBodyComponent &rigidbody,
                            const ColliderComponent &collider) {
          colliders.push_back(
              {entity, &transform, &transform, &rigidbody, &collider});
        });

    state.world->query()
        .read<core::TransformComponent, ColliderComponent>()
        .exclude<RigidBodyComponent>()
        .eachWithEntity([&](ecs::EntityID entity,
                            const core::TransformComponent &transform,
                            const ColliderComponent &collider) {
          colliders.push_back(
              {entity, &transform, nullptr, nullptr, &collider});
        });

    size_t n = colliders.size();
    if (n < 2)
      continue;

    // Broad phase: sweep and prune on AABBs to find candidate pairs.
    std::vector<Aabb> bounds(n);
    for (size_t i = 0; i < n; ++i)
      bounds[i] =
          computeColliderAabb(*colliders[i].collider, *colliders[i].transform);
    const std::vector<std::pair<size_t, size_t>> candidates =
        sweepAndPrune(bounds);
    const size_t numCandidates = candidates.size();
    if (numCandidates == 0)
      continue;

    // Narrow phase: test candidates in parallel chunks.
    size_t numThreads = state.world->threadPool().threadCount();
    size_t chunkSize = std::max(size_t(1), numCandidates / numThreads);

    std::vector<std::vector<CollisionPair>> threadCollisions(numThreads);

    std::vector<std::future<void>> futures;
    for (size_t t = 0; t < numThreads; ++t) {
      size_t pStart = t * chunkSize;
      size_t pEnd = (t == numThreads - 1) ? numCandidates : pStart + chunkSize;

      if (pStart >= numCandidates)
        break;

      futures.push_back(state.world->threadPool().submit([&, t, pStart,
                                                          pEnd]() {
        for (size_t p = pStart; p < pEnd; ++p) {
          const size_t i = candidates[p].first;
          const size_t k = candidates[p].second;
          CollisionInfo info;
          if (!testCollision(*colliders[i].collider, *colliders[i].transform,
                             *colliders[k].collider, *colliders[k].transform,
                             info))
            continue;

          threadCollisions[t].push_back({i, k, info});
        }
      }));
    }

    for (auto &f : futures)
      f.wait();

    for (size_t t = 0; t < threadCollisions.size(); ++t) {
      for (auto &pair : threadCollisions[t]) {
        resolveCollision(colliders[pair.indexA], colliders[pair.indexB],
                         pair.info);

        state.world->emitEvent<events::CollisionEvent>(
            {.entityA = colliders[pair.indexA].entity,
             .entityB = colliders[pair.indexB].entity,
             .info = pair.info});
      }
    }
  }
}

} // namespace componeng::physics
