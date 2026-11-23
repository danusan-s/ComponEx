#pragma once

#include "componeng/ecs/entity.hpp"

namespace componeng::events {

struct EntityCreateEvent {
  ecs::EntityID entity = ecs::INVALID_ENTITY;
};

struct EntityDestroyEvent {
  ecs::EntityID entity = ecs::INVALID_ENTITY;
};

} // namespace componeng::events
