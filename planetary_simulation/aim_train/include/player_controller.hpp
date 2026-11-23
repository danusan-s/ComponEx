#pragma once

#include "componeng/ecs/entity.hpp"
#include "componeng/ecs/system.hpp"

namespace aim_train {

class PlayerController : public componeng::ecs::ISystem {
  float m_sensitivity = 0.25f;
  componeng::ecs::EntityID m_playerEntity = 0;

public:
  void onCreate(const componeng::ecs::SystemState &state) override;
  void onUpdate(const componeng::ecs::SystemState &state) override;
};

} // namespace aim_train
