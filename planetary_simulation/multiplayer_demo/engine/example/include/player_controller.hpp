#pragma once

#include "componeng/ecs/system.hpp"

class PlayerController : public componeng::ecs::ISystem {
public:
  void onUpdate(const componeng::ecs::SystemState &state) override;
};
