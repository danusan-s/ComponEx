#pragma once

#include "miniaudio.h"

#include <memory>

namespace componeng::audio {

// One playing (or playable) instance of a sound. Owns its ma_sound and
// uninitializes it on destruction, move-assignment or uninit(). Setters on a
// sound that failed to initialize are harmless no-ops.
class Sound {
private:
  std::unique_ptr<ma_sound> m_sound;
  bool m_initialized = false;

public:
  Sound();
  ~Sound();
  Sound(const Sound &) = delete;
  Sound &operator=(const Sound &) = delete;
  Sound(Sound &&other) noexcept;
  Sound &operator=(Sound &&other) noexcept;

  // flags are MA_SOUND_FLAG_* (e.g. DECODE to keep PCM in memory, STREAM to
  // decode from disk as it plays).
  bool initFromFile(ma_engine &engine, const char *filePath, ma_uint32 flags);
  // Shares the source's decoded data; streams cannot be copied.
  bool initCopy(ma_engine &engine, const Sound &source, ma_uint32 flags);
  void uninit();
  bool isInitialized() const {
    return m_initialized;
  }

  ma_sound *getSound();
  const ma_sound *getSound() const;
  Sound &setPosition(float x, float y, float z);
  Sound &setVolume(float volume);
  Sound &setPitch(float pitch);
  Sound &setLooping(bool loop);
  // Spatialized sounds are positioned relative to the listener; a sound that
  // is not spatialized plays at full volume in the head (music, ambience, UI).
  Sound &setSpatialized(bool spatialized);
  Sound &set3D(float minDistance, float maxDistance);
  bool play();
  // Pauses: a later play() resumes from where it stopped.
  bool stop();
  bool isPlaying() const;
  // True once a non-looping sound has played to its end and gone silent.
  bool isFinished() const;
};

} // namespace componeng::audio
