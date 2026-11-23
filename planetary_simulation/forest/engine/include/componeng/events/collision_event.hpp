#pragma once

#include "componeng/ecs/entity.hpp"
#include "componeng/physics/collision_detection.hpp"

namespace componeng::events {

struct CollisionEvent {
  ecs::EntityID entityA = ecs::INVALID_ENTITY;
  ecs::EntityID entityB = ecs::INVALID_ENTITY;
  physics::CollisionInfo info;
};

} // namespace componeng::events
