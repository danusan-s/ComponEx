#pragma once

#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/component_serializer.hpp"

namespace componeng::physics {

enum class ColliderType { Box, Sphere };

struct ColliderComponent {
  static constexpr const char *component_name = "ColliderComponent";

  ColliderType type = ColliderType::Box;
  core::TransformComponent transform;
  bool isTrigger = false;
};

} // namespace componeng::physics

namespace componeng::ecs {

#define COLLIDER_COMPONENT_FIELDS(F, ctx)                                      \
  F(type, ctx)                                                                 \
  F(transform, ctx)                                                            \
  F(isTrigger, ctx)

SERIALIZABLE_COMPONENT(physics::ColliderComponent, COLLIDER_COMPONENT_FIELDS)

#undef COLLIDER_COMPONENT_FIELDS

} // namespace componeng::ecs
