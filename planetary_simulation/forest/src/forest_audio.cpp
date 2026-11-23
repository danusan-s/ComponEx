#include "forest_audio.hpp"

#include "components.hpp"
#include "forest_config.hpp"
#include "water.hpp"

#include "componeng/audio/audio_component.hpp"
#include "componeng/audio/audio_manager.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"

#include <algorithm>
#include <cmath>

namespace forest {

using namespace componeng;

namespace {

// The recording is quiet (its loudest step peaks near -18 dB) while the
// ambience is mastered hot, so the footsteps get a gain above 1 and the
// ambience sits low.
constexpr float kAmbientVolume = 0.25f;
// Under the surface the ambience is swapped (crossfaded, both tracks keep
// playing in step) for a muffled copy made with ffmpeg ahead of time. That copy
// is gained up in the file, so the same volume applies.
constexpr float kMuffleRate = 10.0f;  // 1/s: how fast the crossfade follows the head
constexpr float kFootstepVolume = 2.4f;
// The wading recording is much hotter: its steps peak around -2 dB.
constexpr float kWaterStepVolume = 0.6f;

// The swimming recordings are quieter still (peaks near -21 dB). The underwater
// one is the same take, deepened and low-passed with ffmpeg ahead of time, the
// same length so the two swap at any moment without the stroke jumping.
constexpr float kSwimPace = 2.8f;  // m/s the swimming recording is matched to
constexpr float kSwimVolume = 5.0f;
constexpr float kSwimUnderVolume = 5.5f;

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
  audio.registerSound(assetPath("sounds/ambient_sounds.mp3").c_str(),
                      "forest_ambient", true);
  audio.registerSound(assetPath("sounds/underwater_ambient.mp3").c_str(),
                      "forest_underwater_ambient", true);
  audio.registerSound(assetPath("sounds/dirt_footsteps.mp3").c_str(),
                      "forest_footsteps");
  // Four minutes: decoded it would be tens of MB of PCM, so it streams.
  audio.registerSound(assetPath("sounds/water_footsteps.mp3").c_str(),
                      "forest_water_footsteps", true);

  ecs::EntityID ambient = world.createEntity();
  world.addComponents(ambient, core::TransformComponent{},
                      audio::AudioComponent{.audioName = "forest_ambient",
                                            .playOnAwake = true,
                                            .loop = true,
                                            .volume = kAmbientVolume,
                                            .is3D = false});

  // Plays all the time too, silent, so it is at the same point in the track as
  // the other whenever the head goes under.
  ecs::EntityID ambientUnder = world.createEntity();
  world.addComponents(ambientUnder, core::TransformComponent{},
                      audio::AudioComponent{.audioName = "forest_underwater_ambient",
                                            .playOnAwake = true,
                                            .loop = true,
                                            .volume = 0.0f,
                                            .is3D = false});

  // Starts silent: FootstepSystem switches it on while the player walks.
  ecs::EntityID steps = world.createEntity();
  world.addComponents(steps, core::TransformComponent{},
                      audio::AudioComponent{.audioName = "forest_footsteps",
                                            .playOnAwake = false,
                                            .loop = true,
                                            .volume = kFootstepVolume,
                                            .is3D = false});

  ecs::EntityID splashes = world.createEntity();
  world.addComponents(splashes, core::TransformComponent{},
                      audio::AudioComponent{.audioName = "forest_water_footsteps",
                                            .playOnAwake = false,
                                            .loop = true,
                                            .volume = kWaterStepVolume,
                                            .is3D = false});

  audio.registerSound(assetPath("sounds/swimming.mp3").c_str(), "forest_swimming");
  audio.registerSound(assetPath("sounds/underwater_swimming.mp3").c_str(),
                      "forest_underwater_swimming");
  ecs::EntityID swim = world.createEntity();
  world.addComponents(swim, core::TransformComponent{},
                      audio::AudioComponent{.audioName = "forest_swimming",
                                            .playOnAwake = false,
                                            .loop = true,
                                            .volume = kSwimVolume,
                                            .is3D = false});
  ecs::EntityID swimUnder = world.createEntity();
  world.addComponents(swimUnder, core::TransformComponent{},
                      audio::AudioComponent{.audioName = "forest_underwater_swimming",
                                            .playOnAwake = false,
                                            .loop = true,
                                            .volume = kSwimUnderVolume,
                                            .is3D = false});
  world.setResource(FootstepAudio{steps, splashes, swim, swimUnder, ambient, ambientUnder});
}

void FootstepSystem::onUpdate(const ecs::SystemState &state) {
  ecs::World &world = *state.world;
  const FootstepAudio &ids = world.getResource<FootstepAudio>();
  if (!world.hasComponent<audio::AudioComponent>(ids.dirt) ||
      !world.hasComponent<audio::AudioComponent>(ids.water))
    return;
  auto &dirt = world.getComponent<audio::AudioComponent>(ids.dirt);
  auto &water = world.getComponent<audio::AudioComponent>(ids.water);
  if (!world.hasComponent<audio::AudioComponent>(ids.swim) ||
      !world.hasComponent<audio::AudioComponent>(ids.swimUnder))
    return;
  auto &swim = world.getComponent<audio::AudioComponent>(ids.swim);
  auto &swimUnder = world.getComponent<audio::AudioComponent>(ids.swimUnder);
  const float t = float(world.time);
  const float dt = state.deltaTime;
  if (!world.hasComponent<audio::AudioComponent>(ids.ambient) ||
      !world.hasComponent<audio::AudioComponent>(ids.ambientUnder))
    return;
  auto &ambient = world.getComponent<audio::AudioComponent>(ids.ambient);
  auto &ambientUnder = world.getComponent<audio::AudioComponent>(ids.ambientUnder);

  world.query()
      .read<PlayerComponent, core::TransformComponent>()
      .each([&](const PlayerComponent &p, const core::TransformComponent &tf) {
        float planar =
            std::sqrt(p.velocity.x * p.velocity.x + p.velocity.z * p.velocity.z);
        bool walking = p.onGround && !p.swimming && planar > kMovingSpeed;
        // Feet below the surface: wading, so splashes instead of dirt.
        bool wading = tf.position.y < cfg::kWaterLevel;
        dirt.play = walking && !wading;
        water.play = walking && wading;

        float pace = planar / kWalkSpeed;
        dirt.pitch = water.pitch = std::clamp(pace, 0.7f, 1.45f);
        float level = std::clamp(pace, 0.4f, 1.0f);
        dirt.volume = kFootstepVolume * level;
        water.volume = kWaterStepVolume * level;

        // Swimming: the surface recording while the head is out of the water,
        // the muffled one once it is under.
        float speed = std::sqrt(planar * planar + p.velocity.y * p.velocity.y);
        bool stroking = p.swimming && speed > kMovingSpeed;
        bool headUnder =
            depthBelowSurface(tf.position + core::Vec3(0.0f, p.eye, 0.0f), t) > 0.0f;
        swim.play = stroking && !headUnder;
        swimUnder.play = stroking && headUnder;

        // Ambience: crossfade to the muffled copy while the head is under. Equal
        // power, so the level holds in the middle of the fade.
        m_under += ((headUnder ? 1.0f : 0.0f) - m_under) * (1.0f - std::exp(-kMuffleRate * dt));
        ambient.volume = kAmbientVolume * std::cos(m_under * 1.5707963f);
        ambientUnder.volume = kAmbientVolume * std::sin(m_under * 1.5707963f);

        swim.pitch = swimUnder.pitch = std::clamp(speed / kSwimPace, 0.8f, 1.3f);
      });
}

} // namespace forest
