#pragma once

#include "componeng/core/game.hpp"

namespace forest {

// A walkable forest: heightfield terrain, photoscanned trees with LOD and wind,
// instanced grass that follows the player, baked sun shadows, and a
// first-person controller. Everything here is game code on top of the engine.
class ForestGame : public componeng::core::IGame {
public:
  void init(componeng::ecs::World &world) override;
  void shutdown(componeng::ecs::World &world) override;
};

} // namespace forest
