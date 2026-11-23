#define MINIAUDIO_IMPLEMENTATION

#include "componeng/audio/audio_manager.hpp"
#include "componeng/core/types.hpp"

#include "componeng/utils/logger.hpp"
#include <miniaudio.h>

#include <stdexcept>

namespace componeng::audio {

bool AudioManager::init(bool headless) {
  if (m_initialized) {
    return true;
  }
  ma_engine_config config = ma_engine_config_init();
  if (headless) {
    config.noDevice = MA_TRUE;
    config.channels = 2;
    config.sampleRate = 48000;
  }
  ma_result result = ma_engine_init(&config, &m_audioEngine);
  if (result != MA_SUCCESS) {
    LOG_ERROR("Failed to initialize audio engine");
    return false;
  }
  m_initialized = true;
  ma_engine_listener_set_position(&m_audioEngine, 0, 0, 0, 0);
  LOG_INFO("Audio engine initialized");
  return true;
}

void AudioManager::shutdown() {
  if (!m_initialized) {
    return;
  }
  // Sounds must go before the engine they were created from.
  m_instances.clear();
  m_freeSlots.clear();
  m_soundResources.clear();
  ma_engine_uninit(&m_audioEngine);
  m_initialized = false;
  LOG_INFO("Audio engine shutdown");
}

void AudioManager::setMasterVolume(float volume) {
  if (m_initialized) {
    ma_engine_set_volume(&m_audioEngine, volume);
  }
}

void AudioManager::setListenerPosition(float x, float y, float z) {
  if (m_initialized) {
    ma_engine_listener_set_position(&m_audioEngine, 0, x, y, z);
  }
}

void AudioManager::setListenerOrientation(float forwardX, float forwardY,
                                          float forwardZ, float upX, float upY,
                                          float upZ) {
  if (!m_initialized) {
    return;
  }
  ma_engine_listener_set_direction(&m_audioEngine, 0, forwardX, forwardY,
                                   forwardZ);
  ma_engine_listener_set_world_up(&m_audioEngine, 0, upX, upY, upZ);
}

bool AudioManager::registerSound(const char *filePath, core::Name name,
                                 bool stream) {
  if (!m_initialized) {
    return false;
  }
  if (!filePath) {
    LOG_ERROR("Failed to register sound from file: (null)");
    return false;
  }

  Resource resource;
  resource.path = filePath;
  resource.stream = stream;

  // Loading it now reports a bad path at registration, not at first play. A
  // streamed sound's decoder is not worth keeping open, so that probe is
  // dropped again.
  ma_uint32 flags = stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
  if (!resource.master.initFromFile(m_audioEngine, filePath, flags)) {
    LOG_ERROR("Failed to register sound from file: %s", filePath);
    return false;
  }
  if (stream) {
    resource.master.uninit();
  }

  m_soundResources.insert_or_assign(name, std::move(resource));
  return true;
}

bool AudioManager::hasSound(core::Name name) const {
  return m_soundResources.find(name) != m_soundResources.end();
}

core::HandleID AudioManager::createSound(core::Name name, bool spatialized,
                                         bool autoRelease) {
  if (!m_initialized) {
    return core::INVALID_HANDLE;
  }
  auto it = m_soundResources.find(name);
  if (it == m_soundResources.end()) {
    LOG_ERROR("Cannot create sound: '%s' was never registered", name.c_str());
    return core::INVALID_HANDLE;
  }
  const Resource &resource = it->second;

  Sound sound;
  bool ok = resource.stream
                ? sound.initFromFile(m_audioEngine, resource.path.c_str(),
                                     MA_SOUND_FLAG_STREAM)
                : sound.initCopy(m_audioEngine, resource.master, 0);
  if (!ok) {
    LOG_ERROR("Failed to create sound '%s'", name.c_str());
    return core::INVALID_HANDLE;
  }
  sound.setSpatialized(spatialized);

  core::HandleID id;
  if (!m_freeSlots.empty()) {
    id = m_freeSlots.back();
    m_freeSlots.pop_back();
  } else {
    id = static_cast<core::HandleID>(m_instances.size());
    m_instances.emplace_back();
  }
  Instance &instance = m_instances[id];
  instance.sound = std::move(sound);
  instance.inUse = true;
  instance.autoRelease = autoRelease;
  return id;
}

void AudioManager::releaseSound(core::HandleID id) {
  if (!isValid(id)) {
    return;
  }
  Instance &instance = m_instances[id];
  instance.sound.uninit();
  instance.inUse = false;
  instance.autoRelease = false;
  m_freeSlots.push_back(id);
}

bool AudioManager::isValid(core::HandleID id) const {
  return id < m_instances.size() && m_instances[id].inUse;
}

Sound &AudioManager::getSound(core::HandleID id) {
  if (isValid(id)) {
    return m_instances[id].sound;
  }
  LOG_ERROR("Sound with ID %u not found", id);
  throw std::out_of_range("Sound ID out of range");
}

bool AudioManager::playSound(core::HandleID id) {
  if (!isValid(id)) {
    LOG_ERROR("Sound with ID %u not found", id);
    return false;
  }
  if (!m_instances[id].sound.play()) {
    LOG_ERROR("Failed to play sound with ID %u", id);
    return false;
  }
  return true;
}

bool AudioManager::stopSound(core::HandleID id) {
  return isValid(id) && m_instances[id].sound.stop();
}

bool AudioManager::isPlaying(core::HandleID id) const {
  return isValid(id) && m_instances[id].sound.isPlaying();
}

core::HandleID AudioManager::playOneShot(core::Name name, float volume,
                                         float pitch) {
  core::HandleID id = createSound(name, false, true);
  if (id == core::INVALID_HANDLE) {
    return id;
  }
  m_instances[id].sound.setVolume(volume).setPitch(pitch);
  if (!playSound(id)) {
    releaseSound(id);
    return core::INVALID_HANDLE;
  }
  return id;
}

core::HandleID AudioManager::playOneShotAt(core::Name name, float x, float y,
                                           float z, float volume, float pitch,
                                           float minDistance,
                                           float maxDistance) {
  core::HandleID id = createSound(name, true, true);
  if (id == core::INVALID_HANDLE) {
    return id;
  }
  m_instances[id]
      .sound.setPosition(x, y, z)
      .set3D(minDistance, maxDistance)
      .setVolume(volume)
      .setPitch(pitch);
  if (!playSound(id)) {
    releaseSound(id);
    return core::INVALID_HANDLE;
  }
  return id;
}

void AudioManager::cleanupFinishedSounds() {
  for (core::HandleID id = 0; id < m_instances.size(); ++id) {
    const Instance &instance = m_instances[id];
    if (instance.inUse && instance.autoRelease && instance.sound.isFinished()) {
      releaseSound(id);
    }
  }
}

} // namespace componeng::audio
