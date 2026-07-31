#include "collision_system.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/events/collision_event.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/resources/audio_engine.hpp"

using namespace componeng::renderer;

namespace planetary_sim {

void CollisionSystem::onUpdate(const componeng::ecs::SystemState &state) {
  auto &collisionEvents =
      state.world->get_events<componeng::events::CollisionEvent>();
  auto &assetManager =
      state.world->get_resource<componeng::renderer::AssetManager>();
  auto &audioEngine =
      state.world->get_resource<componeng::resources::AudioEngine>();

  for (const auto &event : collisionEvents) {
    // For simplicity, we just play a sound for every collision event.
    // In a real implementation, you would want to check the collision impulse,
    // materials involved, etc. to determine which sound to play and at what
    // volume.
    auto &a_transform =
        state.world->getComponent<componeng::components::TransformComponent>(
            event.entityA);
    auto position = a_transform.position -
                    event.info.normal * event.info.penetration * 0.5f;

    std::string soundPath =
        assetManager.getAudio(assetManager.getAudioID("boop"));
    audioEngine.playSoundFromFile(soundPath.c_str(), position.x, position.y,
                                  position.z, 0.8f, 1.0f, false, 1.0f, 1000.0f);
  }
}

} // namespace planetary_sim
