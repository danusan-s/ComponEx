#pragma once

#include "componeng/ecs/system.hpp"

namespace forest {

// First-person walking and swimming controller. In water deep enough to swim in
// the player moves where they look, but stays in the water (looking up at the
// surface does not fly); touching the ground in shallow water switches back to
// walking. Runs before the engine's CameraSystem so
// the view it computes is this frame's.
//
// Collision is the game's own: the engine's physics has boxes and spheres only,
// no heightfield, no character controller, and tests every collider against
// every other, which a forest of thousands of trunks would drown. Here the
// ground is the terrain's height function and obstacles are vertical cylinders
// looked up in a grid.
class PlayerSystem : public componeng::ecs::ISystem {
public:
  void onUpdate(const componeng::ecs::SystemState &state) override;
};

} // namespace forest
