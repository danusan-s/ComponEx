#pragma once

#include "componeng/core/types.hpp"

namespace componeng::physics {

struct PhysicsConfig {
  core::Vec3 gravity = core::Vec3(0.0f, -9.81f, 0.0f);
  float fixedTimeStep = 1.0f / 60.0f;
};

} // namespace componeng::physics
