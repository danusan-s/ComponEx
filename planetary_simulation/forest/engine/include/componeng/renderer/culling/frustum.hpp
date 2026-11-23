#pragma once

#include "componeng/core/types.hpp"

#include <array>

namespace componeng::renderer {
struct FrustumPlane {
  core::Vec3 normal = core::Vec3(0.0f);
  float distance = 0.0f;
};

class Frustum {
private:
  std::array<FrustumPlane, 6> m_planes;

public:
  Frustum(const core::Mat4 &m);
  std::array<FrustumPlane, 6> getPlanes();
  bool isBoxInFrustum(const core::Vec3 &boxMin, const core::Vec3 &boxMax);
};

}; // namespace componeng::renderer
