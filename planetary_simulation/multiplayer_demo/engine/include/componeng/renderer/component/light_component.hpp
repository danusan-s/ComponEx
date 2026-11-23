#pragma once

#include "componeng/core/types.hpp"

namespace componeng::renderer {

struct DirectionalLightComponent {
  constexpr static const char *component_name = "DirectionalLightComponent";

  core::Vec3 direction = core::Vec3(0.0f, -1.0f, 0.0f);
  core::Vec3 color = core::Vec3(1.0f);
};

struct PointLightComponent {
  constexpr static const char *component_name = "PointLightComponent";

  core::Vec3 position = core::Vec3(0.0f);
  core::Vec3 color = core::Vec3(1.0f);
  float intensity = 1.0f;
  float radius = 10.0f;
};

} // namespace componeng::renderer
