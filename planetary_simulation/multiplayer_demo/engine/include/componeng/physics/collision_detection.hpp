#pragma once

#include "componeng/core/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/physics/collider_component.hpp"

namespace componeng::physics {

/**
 * @brief Output of a collision test: contact normal and penetration depth.
 *
 * The normal points from collider A toward collider B.
 */
struct CollisionInfo {
  core::Vec3 normal = core::Vec3(0.0f);
  float penetration = 0.0f;
};

/**
 * @brief Computes the world-space transform of a collider given its local
 * transform and entity transform.
 */
core::TransformComponent
getWorldTransform(const core::TransformComponent &localTransform,
                  const core::TransformComponent &parentTransform);

/**
 * @brief Tests two axis-aligned bounding boxes for overlap.
 * @return true if overlapping, with contact info filled into @p info.
 */
bool testBoxBox(const core::TransformComponent &a,
                const core::TransformComponent &b, CollisionInfo &info);

/**
 * @brief Tests two spheres for overlap.
 * @return true if overlapping, with contact info filled into @p info.
 */
bool testSphereSphere(const core::TransformComponent &a,
                      const core::TransformComponent &b, CollisionInfo &info);

/**
 * @brief Tests a Box and a sphere for overlap.
 * @return true if overlapping, with contact info filled into @p info.
 */
bool testBoxSphere(const core::TransformComponent &box,
                   const core::TransformComponent &sphere, CollisionInfo &info);

/**
 * @brief Tests a sphere and a Box for overlap (reverse of testBoxSphere).
 *
 * Swaps param and calls testBoxSphere. Reverses normal direction in @p info to
 * maintain A->B convention.
 *
 * @return true if overlapping, with contact info filled into @p info.
 */
bool testSphereBox(const core::TransformComponent &sphere,
                   const core::TransformComponent &box, CollisionInfo &info);

/**
 * @brief Dispatches to the appropriate shape-pair test based on collider types.
 *
 * Supports Box-Box, Sphere-Sphere, and Box-Sphere pairs.
 * Returns false for unsupported combinations.
 */
bool testCollision(const ColliderComponent &colliderA,
                   const core::TransformComponent &transformA,
                   const ColliderComponent &colliderB,
                   const core::TransformComponent &transformB,
                   CollisionInfo &info);

} // namespace componeng::physics
