#pragma once

#include "componeng/core/types.hpp"
#include "componeng/ecs/component_serializer.hpp"

namespace componeng::physics {

struct RigidBodyComponent {
  static constexpr const char *component_name = "RigidBodyComponent";

  enum class Type {
    Static,   // Immovable object, not affected by forces
    Dynamic,  // Affected by forces, can move and rotate
    Kinematic // Moves according to user velocities, not affected by forces
  };

  Type type = Type::Static;
  core::Vec3 velocity = core::Vec3(0.0f);
  float mass = 1.0f;
  float restitution = 0.5f;
};

} // namespace componeng::physics

namespace componeng::ecs {

#define RIGIDBODY_COMPONENT_FIELDS(F, ctx)                                     \
  F(type, ctx)                                                                 \
  F(velocity, ctx)                                                             \
  F(mass, ctx)                                                                 \
  F(restitution, ctx)

SERIALIZABLE_COMPONENT(physics::RigidBodyComponent, RIGIDBODY_COMPONENT_FIELDS)

#undef RIGIDBODY_COMPONENT_FIELDS

} // namespace componeng::ecs
