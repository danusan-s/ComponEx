#pragma once

#include "componeng/ecs/system.hpp"

namespace planetary_sim {

class CollisionSystem : public componeng::ecs::ISystem {
public:
  void onUpdate(const componeng::ecs::SystemState &state) override;
};

} // namespace planetary_sim
