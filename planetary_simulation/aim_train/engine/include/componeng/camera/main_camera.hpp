#pragma once

#include "componeng/ecs/entity.hpp"

namespace componeng::camera {

struct MainCamera {
  ecs::EntityID entity = ecs::INVALID_ENTITY;
};

} // namespace componeng::camera
