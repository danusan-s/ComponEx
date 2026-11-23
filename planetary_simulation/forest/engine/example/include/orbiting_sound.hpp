#pragma once

#include "componeng/ecs/system.hpp"

class OrbitingSound : public componeng::ecs::ISystem {
public:
  void onUpdate(const componeng::ecs::SystemState &state) override;
};
