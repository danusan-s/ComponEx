#pragma once

#include "componeng/core/plugin.hpp"
#include "componeng/core/window_plugin.hpp"

#include <typeindex>
#include <vector>

namespace componeng::core {

// What a normal game wants: WindowPlugin, InputPlugin, PhysicsPlugin,
// CameraPlugin, AudioPlugin and RenderPlugin, in that order.
//
//   DefaultPlugins plugins;
//   plugins.window.title = "My game";
//   plugins.disable<audio::AudioPlugin>();
//   engine.add(plugins);
//
// A disabled plugin can be added on its own, configured or replaced by your
// own. Disabling WindowPlugin while RenderPlugin stays on does not work:
// RenderPlugin requires it, and gets a default one.
class DefaultPlugins final : public IPlugin {
public:
  WindowPlugin window;

  template <typename P> DefaultPlugins &disable() {
    m_disabled.push_back(typeid(P));
    return *this;
  }

  void build(Engine &engine) override;

private:
  template <typename P> void addUnlessDisabled(Engine &engine, P plugin);

  std::vector<std::type_index> m_disabled;
};

} // namespace componeng::core
