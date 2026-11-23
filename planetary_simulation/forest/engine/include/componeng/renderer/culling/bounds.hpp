#pragma once

#include "componeng/core/types.hpp"

#include <algorithm>

namespace componeng::renderer {

/** Axis-aligned bounding box. */
struct Aabb {
  core::Vec3 min = core::Vec3(0.0f);
  core::Vec3 max = core::Vec3(0.0f);
};

/**
 * @brief World-space AABB of a local-space AABB under an affine transform.
 *
 * Arvo's method: each world axis' extent is the sum, over the three local
 * axes, of the smaller/larger of the two products m * min and m * max. That
 * is exact for any rotation, scale (including negative) and translation, and
 * costs a handful of multiplies, with no need to transform the 8 corners.
 * `padding` inflates the result on every side, for geometry that a vertex
 * shader moves outside the mesh's own bounds (billboards, wind, waves).
 */
inline Aabb transformAabb(const Aabb &local, const core::Mat4 &m,
                          float padding = 0.0f) {
  const float lo[3] = {local.min.x, local.min.y, local.min.z};
  const float hi[3] = {local.max.x, local.max.y, local.max.z};

  float outMin[3];
  float outMax[3];
  for (int axis = 0; axis < 3; ++axis) {
    // m[col][row]: row `axis` of the matrix is the world axis being built.
    float mn = m[3][axis];
    float mx = m[3][axis];
    for (int col = 0; col < 3; ++col) {
      const float a = m[col][axis] * lo[col];
      const float b = m[col][axis] * hi[col];
      mn += std::min(a, b);
      mx += std::max(a, b);
    }
    outMin[axis] = mn - padding;
    outMax[axis] = mx + padding;
  }

  Aabb result;
  result.min = core::Vec3(outMin[0], outMin[1], outMin[2]);
  result.max = core::Vec3(outMax[0], outMax[1], outMax[2]);
  return result;
}

} // namespace componeng::renderer
