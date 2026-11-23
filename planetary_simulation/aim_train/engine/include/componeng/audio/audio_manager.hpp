#pragma once

#include "componeng/audio/sound.hpp"
#include "componeng/core/types.hpp"
#include "miniaudio.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace componeng::audio {

// Manages audio engine lifecycle, registered sound files and live instances.
//
// A sound is registered once by name, then instantiated: createSound() gives a
// handle to a fresh instance that can be played, stopped and positioned
// independently. Handles stay valid until the instance is released.
//
// Two lifetimes:
//  - createSound(name, ..., autoRelease = true) is fire-and-forget: the
//    instance is reclaimed once it has played to its end.
//  - autoRelease = false (what AudioComponent uses) is kept until
//    releaseSound(), so it can be stopped and restarted.
class AudioManager {
public:
  AudioManager() = default;
  ~AudioManager() = default;

  AudioManager(const AudioManager &) = delete;
  AudioManager &operator=(const AudioManager &) = delete;
  AudioManager(AudioManager &&) = default;
  AudioManager &operator=(AudioManager &&) = default;

  // headless: no output device; the engine only advances when
  // ma_engine_read_pcm_frames() is called (tests, machines without sound).
  // Returns false if the audio engine could not start; every other call is
  // then a harmless no-op that reports failure.
  bool init(bool headless = false);
  void shutdown();
  bool isInitialized() const {
    return m_initialized;
  }
  ma_engine &getEngine() {
    return m_audioEngine;
  }

  void setMasterVolume(float volume);
  void setListenerPosition(float x, float y, float z);
  void setListenerOrientation(float forwardX, float forwardY, float forwardZ,
                              float upX, float upY, float upZ);

  // stream: decode from disk while playing instead of holding all the PCM in
  // memory. Use it for long files (music, ambience); short effects are better
  // decoded up front.
  bool registerSound(const char *filePath, core::Name name,
                     bool stream = false);
  bool hasSound(core::Name name) const;

  // spatialized: positioned and attenuated relative to the listener; otherwise
  // it plays at full volume wherever the listener is. Returns INVALID_HANDLE
  // if the name was never registered.
  core::HandleID createSound(core::Name name, bool spatialized = true,
                             bool autoRelease = true);
  void releaseSound(core::HandleID id);
  bool isValid(core::HandleID id) const;
  // Throws std::out_of_range for a handle that is not valid.
  Sound &getSound(core::HandleID id);

  bool playSound(core::HandleID id);
  bool stopSound(core::HandleID id);
  bool isPlaying(core::HandleID id) const;

  // Fire-and-forget helpers: a non-spatialized one-shot, or one at a position.
  core::HandleID playOneShot(core::Name name, float volume = 1.0f,
                             float pitch = 1.0f);
  core::HandleID playOneShotAt(core::Name name, float x, float y, float z,
                               float volume = 1.0f, float pitch = 1.0f,
                               float minDistance = 1.0f,
                               float maxDistance = 100.0f);

  // Reclaims auto-release instances that have finished. AudioSystem calls it
  // every frame.
  void cleanupFinishedSounds();

private:
  struct Resource {
    std::string path;
    bool stream = false;
    Sound master; // decoded data every non-streamed instance copies from
  };

  struct Instance {
    Sound sound;
    bool inUse = false;
    bool autoRelease = false;
  };

  bool m_initialized = false;
  ma_engine m_audioEngine;
  std::unordered_map<core::Name, Resource> m_soundResources;

  std::vector<Instance> m_instances;
  std::vector<core::HandleID> m_freeSlots;
};

} // namespace componeng::audio
