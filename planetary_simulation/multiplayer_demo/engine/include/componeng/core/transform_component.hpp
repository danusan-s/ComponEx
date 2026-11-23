#pragma once

#include "componeng/core/types.hpp"
#include "componeng/ecs/component_serializer.hpp"

#include <cmath>

namespace componeng::core {

struct TransformComponent {
  static constexpr const char *component_name = "TransformComponent";

  core::Vec3 position = core::Vec3(0.0f, 0.0f, 0.0f);
  core::Vec3 rotation = core::Vec3(0.0f, 0.0f, 0.0f); // Euler angles in degrees
  core::Vec3 scale = core::Vec3(1.0f, 1.0f, 1.0f);

  /**
   * @brief Model matrix: T * Rx * Ry * Rz * S, with rotation in degrees.
   *
   * Equal to composing translate/rotate/scale in that order, but built in one
   * pass: sin/cos once per non-zero axis (none at all for an unrotated
   * transform) and no intermediate 4x4 products. Called once per rendered
   * entity per frame, so it is worth keeping allocation- and product-free.
   */
  core::Mat4 toMatrix() const {
    float sx, cx, sy, cy, sz, cz;
    sinCosDegrees(rotation.x, sx, cx);
    sinCosDegrees(rotation.y, sy, cy);
    sinCosDegrees(rotation.z, sz, cz);

    // Rows of R = Rx * Ry * Rz.
    const float r00 = cy * cz;
    const float r01 = -cy * sz;
    const float r02 = sy;
    const float r10 = cx * sz + sx * sy * cz;
    const float r11 = cx * cz - sx * sy * sz;
    const float r12 = -sx * cy;
    const float r20 = sx * sz - cx * sy * cz;
    const float r21 = sx * cz + cx * sy * sz;
    const float r22 = cx * cy;

    // Column-major: m[column][row]. Column j of R is scaled by scale[j].
    core::Mat4 m;
    m[0][0] = r00 * scale.x;
    m[0][1] = r10 * scale.x;
    m[0][2] = r20 * scale.x;
    m[0][3] = 0.0f;
    m[1][0] = r01 * scale.y;
    m[1][1] = r11 * scale.y;
    m[1][2] = r21 * scale.y;
    m[1][3] = 0.0f;
    m[2][0] = r02 * scale.z;
    m[2][1] = r12 * scale.z;
    m[2][2] = r22 * scale.z;
    m[2][3] = 0.0f;
    m[3][0] = position.x;
    m[3][1] = position.y;
    m[3][2] = position.z;
    m[3][3] = 1.0f;
    return m;
  }

private:
  static void sinCosDegrees(float degrees, float &s, float &c) {
    if (degrees == 0.0f) {
      s = 0.0f;
      c = 1.0f;
      return;
    }
    const float radiansValue = core::radians(degrees);
    s = std::sin(radiansValue);
    c = std::cos(radiansValue);
  }
};

} // namespace componeng::core

namespace componeng::ecs {

#define TRANSFORM_COMPONENT_FIELDS(F, ctx)                                     \
  F(position, ctx)                                                             \
  F(rotation, ctx)                                                             \
  F(scale, ctx)

SERIALIZABLE_COMPONENT(core::TransformComponent, TRANSFORM_COMPONENT_FIELDS)

#undef TRANSFORM_COMPONENT_FIELDS

} // namespace componeng::ecs
