#pragma once

#include "componeng/ecs/entity.hpp"
#include "componeng/ecs/system.hpp"

namespace componeng::ecs {
class World;
}

namespace forest {

// Registers the forest's sounds and spawns the entities that play them: a
// looping ambience and the footsteps (silent until the player walks).
void setupForestAudio(componeng::ecs::World &world);

struct FootstepAudio {
  componeng::ecs::EntityID dirt = componeng::ecs::INVALID_ENTITY;
  componeng::ecs::EntityID water = componeng::ecs::INVALID_ENTITY;
};

// Plays the footsteps while the player is walking on the ground, faster and
// louder the faster they go: dirt on land, splashing once the feet are below
// the water's surface. Runs after PlayerSystem and before AudioSystem,
// which applies what it asks for.
class FootstepSystem : public componeng::ecs::ISystem {
public:
  void onUpdate(const componeng::ecs::SystemState &state) override;
};

} // namespace forest
