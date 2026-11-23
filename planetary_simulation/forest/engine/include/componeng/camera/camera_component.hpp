#pragma once

#include "componeng/core/types.hpp"
#include "componeng/ecs/component_serializer.hpp"

namespace componeng::camera {

struct CameraComponent {
  static constexpr const char *component_name = "CameraComponent";

  float fov = 60.0f;
  float aspectRatio = 16.0f / 9.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
  core::Mat4 viewProjectionMatrix = core::Mat4(1.0f);
  // Projection matrix (for skybox: projection × rotation-only view).
  core::Mat4 projectionMatrix = core::Mat4(1.0f);
  // Rotation-only view matrix (no camera translation), for skybox rendering.
  // Computed by CameraSystem, not serialized.
  core::Mat4 viewMatrix = core::Mat4(1.0f);
};

} // namespace componeng::camera

namespace componeng::ecs {

#define CAMERA_COMPONENT_FIELDS(F, ctx)                                        \
  F(fov, ctx)                                                                  \
  F(aspectRatio, ctx)                                                          \
  F(nearPlane, ctx)                                                            \
  F(farPlane, ctx)                                                             \
  F(viewProjectionMatrix, ctx)

SERIALIZABLE_COMPONENT(camera::CameraComponent, CAMERA_COMPONENT_FIELDS)

#undef CAMERA_COMPONENT_FIELDS

} // namespace componeng::ecs
