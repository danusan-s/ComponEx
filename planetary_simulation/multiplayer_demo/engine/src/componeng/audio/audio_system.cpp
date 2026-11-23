#include "componeng/audio/audio_system.hpp"

#include "componeng/audio/audio_component.hpp"
#include "componeng/audio/audio_manager.hpp"
#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/world.hpp"

#include <cmath>

namespace componeng::audio {

void AudioSystem::onUpdate(const ecs::SystemState &state) {
  auto &audioManager = state.world->getResource<AudioManager>();
  if (!audioManager.isInitialized()) {
    return;
  }

  auto &mainCamera = state.world->getResource<camera::MainCamera>();
  if (state.world->hasComponent<componeng::core::TransformComponent>(
          mainCamera.entity)) {
    const auto &camTransform =
        state.world->readComponent<componeng::core::TransformComponent>(
            mainCamera.entity);
    audioManager.setListenerPosition(camTransform.position.x,
                                     camTransform.position.y,
                                     camTransform.position.z);

    float cosYaw = cos(core::radians(camTransform.rotation.y));
    float sinYaw = sin(core::radians(camTransform.rotation.y));
    float cosPitch = cos(core::radians(camTransform.rotation.x));
    float sinPitch = sin(core::radians(camTransform.rotation.x));
    core::Vec3 front;
    front.x = cosYaw * cosPitch;
    front.y = sinPitch;
    front.z = sinYaw * cosPitch;
    front = core::normalize(front);
    core::Vec3 up = core::normalize(
        core::cross(core::cross(front, core::Vec3(0.0f, 1.0f, 0.0f)), front));
    audioManager.setListenerOrientation(front.x, front.y, front.z, up.x, up.y,
                                        up.z);
  }

  state.world->query()
      .write<AudioComponent>()
      .read<core::TransformComponent>()
      .each([&](AudioComponent &audio,
                const core::TransformComponent &transform) {
        if (audio.audioID == core::INVALID_HANDLE) {
          // Not registered yet: wait quietly, it may be registered later.
          if (!audioManager.hasSound(audio.audioName)) {
            return;
          }
          audio.audioID =
              audioManager.createSound(audio.audioName, audio.is3D, false);
          if (audio.audioID == core::INVALID_HANDLE) {
            return;
          }
          if (audio.playOnAwake) {
            audio.play = true;
          }
        }
        if (!audioManager.isValid(audio.audioID)) {
          return;
        }
        auto &sound = audioManager.getSound(audio.audioID);

        sound.setVolume(audio.volume)
            .setPitch(audio.pitch)
            .setLooping(audio.loop);
        if (audio.is3D) {
          sound
              .setPosition(transform.position.x, transform.position.y,
                           transform.position.z)
              .set3D(audio.minDistance, audio.maxDistance);
        }

        // Follow the requested state. A sound that was playing last frame and
        // is silent now without being asked to stop has reached its end.
        bool playing = sound.isPlaying();
        if (audio.play && !playing) {
          if (audio.isPlaying && !audio.loop) {
            audio.play = false;
          } else {
            audioManager.playSound(audio.audioID);
          }
        } else if (!audio.play && playing) {
          audioManager.stopSound(audio.audioID);
        }
        audio.isPlaying = sound.isPlaying();
      });

  audioManager.cleanupFinishedSounds();
}

} // namespace componeng::audio
