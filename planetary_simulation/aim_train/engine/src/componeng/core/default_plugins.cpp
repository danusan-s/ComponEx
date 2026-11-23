#include "componeng/core/default_plugins.hpp"

#include "componeng/audio/audio_plugin.hpp"
#include "componeng/camera/camera_plugin.hpp"
#include "componeng/core/engine.hpp"
#include "componeng/input/input_plugin.hpp"
#include "componeng/physics/physics_plugin.hpp"
#include "componeng/renderer/render_plugin.hpp"

#include <algorithm>

namespace componeng::core {

template <typename P>
void DefaultPlugins::addUnlessDisabled(Engine &engine, P plugin) {
  if (std::find(m_disabled.begin(), m_disabled.end(),
                std::type_index(typeid(P))) == m_disabled.end()) {
    engine.add(std::move(plugin));
  }
}

void DefaultPlugins::build(Engine &engine) {
  addUnlessDisabled(engine, window);
  addUnlessDisabled(engine, input::InputPlugin{});
  addUnlessDisabled(engine, physics::PhysicsPlugin{});
  addUnlessDisabled(engine, camera::CameraPlugin{});
  addUnlessDisabled(engine, audio::AudioPlugin{});
  addUnlessDisabled(engine, renderer::RenderPlugin{});
}

} // namespace componeng::core
