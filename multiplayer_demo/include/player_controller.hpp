#pragma once

#include "componeng/ecs/entity.hpp"
#include "componeng/ecs/system.hpp"

namespace multiplayer_demo {

// First-person walk/jump/look for the local player; creates its entity and
// drives the main camera.
class PlayerController : public componeng::ecs::ISystem {
  componeng::ecs::EntityID m_player = 0;

public:
  void onCreate(const componeng::ecs::SystemState &state) override;
  void onUpdate(const componeng::ecs::SystemState &state) override;
};

} // namespace multiplayer_demo
