#pragma once

#include "componeng/core/types.hpp"
#include "componeng/ecs/component_serializer.hpp"

namespace componeng::audio {

struct AudioComponent {
  static constexpr const char *component_name = "AudioComponent";

  core::Name audioName;
  // Starts playing as soon as the sound is created (sets `play`).
  bool playOnAwake = true;
  bool loop = false;
  // Runtime output: whether the sound is audible right now.
  bool isPlaying = false;
  // Runtime control, the state you want: set it to start or pause the sound,
  // AudioSystem follows. A sound that is not looping clears it when it ends.
  bool play = false;
  float volume = 1.0f;
  float pitch = 1.0f;
  // false: plays at full volume wherever the listener is (music, ambience).
  bool is3D = false;
  float minDistance = 1.0f;
  float maxDistance = 100.0f;

  // Runtime data (not serialized)
  core::HandleID audioID = core::INVALID_HANDLE;
};

} // namespace componeng::audio

namespace componeng::ecs {

#define AUDIO_COMPONENT_FIELDS(F, ctx)                                         \
  F(audioName, ctx)                                                            \
  F(playOnAwake, ctx)                                                          \
  F(loop, ctx)                                                                 \
  F(volume, ctx)                                                               \
  F(pitch, ctx)                                                                \
  F(is3D, ctx)                                                                 \
  F(minDistance, ctx)                                                          \
  F(maxDistance, ctx)

SERIALIZABLE_COMPONENT(audio::AudioComponent, AUDIO_COMPONENT_FIELDS)

#undef AUDIO_COMPONENT_FIELDS

} // namespace componeng::ecs
