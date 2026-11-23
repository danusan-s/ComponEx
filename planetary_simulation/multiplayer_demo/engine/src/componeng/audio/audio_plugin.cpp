#include "componeng/audio/audio_plugin.hpp"

#include "componeng/audio/audio_component.hpp"
#include "componeng/audio/audio_manager.hpp"
#include "componeng/audio/audio_system.hpp"
#include "componeng/core/engine.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/utils/logger.hpp"

namespace componeng::audio {

void AudioPlugin::build(core::Engine &engine) {
  ecs::World &world = engine.world();
  world.registerComponents<core::TransformComponent, AudioComponent>();
  world.setResource<AudioManager>(AudioManager());
  world.registerSystem<AudioSystem>(ecs::SystemGroup::Simulation);
}

void AudioPlugin::ready(ecs::World &world) {
  world.getResource<AudioManager>().init();
  LOG_INFO("Initialized audio engine");
}

void AudioPlugin::cleanup(ecs::World &world) {
  world.getResource<AudioManager>().shutdown();
}

} // namespace componeng::audio
