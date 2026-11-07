#pragma once

#include "componeng/core/types.hpp"

// Components must be trivially copyable: the ECS relocates them with memcpy.

namespace forest {

struct PlayerComponent {
  static constexpr const char *component_name = "PlayerComponent";

  componeng::core::Vec3 velocity = componeng::core::Vec3(0.0f);
  float yaw = 0.0f;    // degrees, same convention as the camera's rotation.y
  float pitch = 0.0f;  // degrees
  float eye = 1.65f;   // current eye height above the feet (smoothed)
  float smoothY = 0.0f;  // camera height smoothing while walking over bumps
  float bobPhase = 0.0f;
  bool onGround = false;
};

} // namespace forest
