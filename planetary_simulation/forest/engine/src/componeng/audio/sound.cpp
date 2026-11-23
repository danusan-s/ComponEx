#include "componeng/audio/sound.hpp"

namespace componeng::audio {

Sound::Sound() : m_sound(std::make_unique<ma_sound>()) {
}

Sound::~Sound() {
  uninit();
}

Sound::Sound(Sound &&other) noexcept
    : m_sound(std::move(other.m_sound)), m_initialized(other.m_initialized) {
  other.m_initialized = false;
}

Sound &Sound::operator=(Sound &&other) noexcept {
  if (this != &other) {
    uninit();
    m_sound = std::move(other.m_sound);
    m_initialized = other.m_initialized;
    other.m_initialized = false;
  }
  return *this;
}

bool Sound::initFromFile(ma_engine &engine, const char *filePath,
                         ma_uint32 flags) {
  uninit();
  if (!m_sound) {
    m_sound = std::make_unique<ma_sound>();
  }
  m_initialized = ma_sound_init_from_file(&engine, filePath, flags, nullptr,
                                          nullptr, m_sound.get()) == MA_SUCCESS;
  return m_initialized;
}

bool Sound::initCopy(ma_engine &engine, const Sound &source, ma_uint32 flags) {
  uninit();
  if (!source.m_initialized) {
    return false;
  }
  if (!m_sound) {
    m_sound = std::make_unique<ma_sound>();
  }
  m_initialized = ma_sound_init_copy(&engine, source.m_sound.get(), flags,
                                     nullptr, m_sound.get()) == MA_SUCCESS;
  return m_initialized;
}

void Sound::uninit() {
  if (m_sound && m_initialized) {
    ma_sound_uninit(m_sound.get());
  }
  m_initialized = false;
}

ma_sound *Sound::getSound() {
  return m_sound.get();
}

const ma_sound *Sound::getSound() const {
  return m_sound.get();
}

Sound &Sound::setPosition(float x, float y, float z) {
  if (m_initialized) {
    ma_sound_set_position(m_sound.get(), x, y, z);
  }
  return *this;
}

Sound &Sound::setVolume(float volume) {
  if (m_initialized) {
    ma_sound_set_volume(m_sound.get(), volume);
  }
  return *this;
}

Sound &Sound::setPitch(float pitch) {
  if (m_initialized) {
    ma_sound_set_pitch(m_sound.get(), pitch);
  }
  return *this;
}

Sound &Sound::setLooping(bool loop) {
  if (m_initialized) {
    ma_sound_set_looping(m_sound.get(), loop);
  }
  return *this;
}

Sound &Sound::setSpatialized(bool spatialized) {
  if (m_initialized) {
    ma_sound_set_spatialization_enabled(m_sound.get(), spatialized);
  }
  return *this;
}

Sound &Sound::set3D(float minDistance, float maxDistance) {
  if (m_initialized) {
    ma_sound_set_spatialization_enabled(m_sound.get(), MA_TRUE);
    ma_sound_set_min_distance(m_sound.get(), minDistance);
    ma_sound_set_max_distance(m_sound.get(), maxDistance);
    ma_sound_set_attenuation_model(m_sound.get(), ma_attenuation_model_linear);
  }
  return *this;
}

bool Sound::play() {
  return m_initialized && ma_sound_start(m_sound.get()) == MA_SUCCESS;
}

bool Sound::stop() {
  return m_initialized && ma_sound_stop(m_sound.get()) == MA_SUCCESS;
}

bool Sound::isPlaying() const {
  return m_initialized && ma_sound_is_playing(m_sound.get()) == MA_TRUE;
}

bool Sound::isFinished() const {
  return m_initialized && ma_sound_at_end(m_sound.get()) == MA_TRUE &&
         ma_sound_is_playing(m_sound.get()) == MA_FALSE;
}

} // namespace componeng::audio
