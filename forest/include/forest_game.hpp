#pragma once

#include "componeng/core/plugin.hpp"

namespace forest {

// A walkable forest: heightfield terrain, photoscanned trees with LOD and wind,
// instanced grass that follows the player, baked sun shadows, and a
// first-person controller. Everything here is game code on top of the engine,
// packaged as a plugin: add it after DefaultPlugins.
class ForestGame : public componeng::core::IPlugin {
public:
  // Registers the game's components and systems.
  void build(componeng::core::Engine &engine) override;
  // Builds the world and creates the scene (the window and built-in assets
  // exist by now).
  void ready(componeng::ecs::World &world) override;
};

} // namespace forest
