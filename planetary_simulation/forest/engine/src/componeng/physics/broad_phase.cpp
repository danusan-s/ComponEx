#include "componeng/physics/broad_phase.hpp"

#include "componeng/physics/collision_detection.hpp"

#include <algorithm>
#include <numeric>

namespace componeng::physics {

Aabb computeColliderAabb(const ColliderComponent &collider,
                         const core::TransformComponent &entityTransform) {
  const core::TransformComponent world =
      getWorldTransform(collider.transform, entityTransform);

  core::Vec3 half = world.scale;
  if (collider.type == ColliderType::Sphere)
    half = core::Vec3(world.scale.x);

  // Scale may be negative; the narrow phase takes it as-is, so stay
  // conservative with the absolute value.
  half = core::Vec3(std::abs(half.x), std::abs(half.y), std::abs(half.z));

  return {world.position - half, world.position + half};
}

std::vector<std::pair<size_t, size_t>>
sweepAndPrune(const std::vector<Aabb> &bounds) {
  const size_t n = bounds.size();
  std::vector<std::pair<size_t, size_t>> pairs;
  if (n < 2)
    return pairs;

  std::vector<size_t> order(n);
  std::iota(order.begin(), order.end(), size_t(0));
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return bounds[a].min.x < bounds[b].min.x;
  });

  for (size_t i = 0; i < n; ++i) {
    const Aabb &a = bounds[order[i]];
    for (size_t j = i + 1; j < n; ++j) {
      const Aabb &b = bounds[order[j]];
      if (b.min.x > a.max.x)
        break;
      if (a.min.y > b.max.y || b.min.y > a.max.y)
        continue;
      if (a.min.z > b.max.z || b.min.z > a.max.z)
        continue;

      const size_t lo = std::min(order[i], order[j]);
      const size_t hi = std::max(order[i], order[j]);
      pairs.emplace_back(lo, hi);
    }
  }

  // Deterministic order, matching the old brute-force (i, k) iteration.
  std::sort(pairs.begin(), pairs.end());
  return pairs;
}

} // namespace componeng::physics
