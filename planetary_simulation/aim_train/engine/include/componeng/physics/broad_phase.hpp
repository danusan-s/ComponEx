#pragma once

#include "componeng/core/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/physics/collider_component.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace componeng::physics {

/** @brief World-space axis-aligned bounds used by the broad phase. */
struct Aabb {
  core::Vec3 min = core::Vec3(0.0f);
  core::Vec3 max = core::Vec3(0.0f);
};

/**
 * @brief Conservative world-space AABB of a collider.
 *
 * Mirrors the narrow phase: the collider's world transform is composed with
 * its entity transform, a Box uses `scale` as half extents and a Sphere uses
 * `scale.x` as its radius. Rotation is ignored, as in the narrow phase.
 */
Aabb computeColliderAabb(const ColliderComponent &collider,
                         const core::TransformComponent &entityTransform);

/**
 * @brief Sweep and prune broad phase.
 *
 * Sorts the boxes by their minimum on the X axis, sweeps once, and for every
 * box only inspects the following boxes whose min.x is not past its max.x.
 * Candidates are then confirmed on Y and Z. Touching boxes count as
 * overlapping so the result is a superset of what the narrow phase accepts.
 *
 * @param bounds One AABB per collider.
 * @return Index pairs (i, k) with i < k into @p bounds, sorted ascending.
 */
std::vector<std::pair<size_t, size_t>>
sweepAndPrune(const std::vector<Aabb> &bounds);

} // namespace componeng::physics
