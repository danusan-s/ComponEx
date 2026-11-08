#include "forest_audio.hpp"

#include "components.hpp"
#include "forest_config.hpp"

#include "componeng/audio/audio_component.hpp"
#include "componeng/audio/audio_manager.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"

#include <algorithm>
#include <cmath>

namespace forest {

using namespace componeng;

namespace {

// The recording is quiet (its loudest step peaks near -18 dB) while the ambience
// is mastered hot, so the footsteps get a gain above 1 and the ambience sits low.
constexpr float kAmbientVolume = 0.35f;
constexpr float kFootstepVolume = 2.4f;

// Below this planar speed (m/s) the player counts as standing still.
constexpr float kMovingSpeed = 0.8f;
// The walking speed the recording's cadence is matched to; faster or slower
// scales the playback rate (and so the step rate) around it.
constexpr float kWalkSpeed = 4.2f;

} // namespace

void setupForestAudio(ecs::World &world) {
  auto &audio = world.getResource<audio::AudioManager>();
  if (!audio.isInitialized())
    return;

  // Minutes of stereo ambience would sit in memory as raw PCM, so it streams
  // from disk; the footsteps are short enough to decode up front.
  audio.registerSound(assetPath("sounds/ambient_sounds.mp3").c_str(), "forest_ambient", true);
  audio.registerSound(assetPath("sounds/dirt_footsteps.mp3").c_str(), "forest_footsteps");

  ecs::EntityID ambient = world.createEntity();
  world.addComponents(ambient, core::TransformComponent{},
                      audio::AudioComponent{.audioName = "forest_ambient",
                                            .playOnAwake = true,
                                            .loop = true,
                                            .volume = kAmbientVolume,
                                            .is3D = false});

  // Starts silent: FootstepSystem switches it on while the player walks.
  ecs::EntityID steps = world.createEntity();
  world.addComponents(steps, core::TransformComponent{},
                      audio::AudioComponent{.audioName = "forest_footsteps",
                                            .playOnAwake = false,
                                            .loop = true,
                                            .volume = kFootstepVolume,
                                            .is3D = false});
  world.setResource(FootstepAudio{steps});
}

void FootstepSystem::onUpdate(const ecs::SystemState &state) {
  ecs::World &world = *state.world;
  ecs::EntityID steps = world.getResource<FootstepAudio>().entity;
  if (!world.hasComponent<audio::AudioComponent>(steps))
    return;
  auto &sound = world.getComponent<audio::AudioComponent>(steps);

  world.query<PlayerComponent>().each([&](PlayerComponent &p) {
    float planar = std::sqrt(p.velocity.x * p.velocity.x + p.velocity.z * p.velocity.z);
    sound.play = p.onGround && planar > kMovingSpeed;

    float pace = planar / kWalkSpeed;
    sound.pitch = std::clamp(pace, 0.7f, 1.45f);
    sound.volume = kFootstepVolume * std::clamp(pace, 0.4f, 1.0f);
  });
}

} // namespace forest
