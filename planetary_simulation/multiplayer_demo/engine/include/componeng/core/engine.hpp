#pragma once

#include "componeng/core/plugin.hpp"
#include "componeng/ecs/world.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace componeng::core {

/**
 * @brief World resource for asking the main loop to stop. Any system can call
 * requestExit(); the runner checks it each frame.
 */
struct EngineControl {
  bool exitRequested = false;
  void requestExit() {
    exitRequested = true;
  }
};

/**
 * @brief Owns the World and the plugins, and drives the frame loop.
 *
 * The engine itself does nothing visible: a window, rendering, input and the
 * rest each come from a plugin (see DefaultPlugins). With none added it is a
 * bare world, which is also what a headless server or a test wants.
 *
 *   Engine engine;
 *   engine.add(DefaultPlugins{}).add(MyGame{}).run();
 *
 * run() is initialize(), the runner, then shutdown(). Tests and tools can call
 * those themselves and step frames with update().
 */
class Engine {
public:
  // Runs the frame loop: it calls Engine::update once per frame. A plugin that
  // owns a window sets one that polls it; without one, run() uses a fixed-rate
  // loop with no window.
  using Runner = std::function<void(Engine &)>;

  Engine();
  ~Engine();
  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;
  Engine(Engine &&) = delete;
  Engine &operator=(Engine &&) = delete;

  // Builds the plugin now. Adding a type twice, or after initialize(), throws.
  template <typename P> Engine &add(P plugin) {
    static_assert(std::is_base_of_v<IPlugin, P>, "plugins derive from IPlugin");
    addPlugin(typeid(P), std::make_unique<P>(std::move(plugin)));
    return *this;
  }

  // For use inside build(): makes sure P is built before the caller. Adds a
  // default P if none was added yet, and throws if P has no default constructor
  // or is itself waiting on the caller.
  template <typename P> void require() {
    switch (stateOf(typeid(P))) {
      case PluginState::Built:
        return;
      case PluginState::Building:
        throw std::runtime_error("Plugin dependency cycle at '" + nameOf<P>() +
                                 "'");
      case PluginState::Absent:
        break;
    }
    if constexpr (std::is_default_constructible_v<P>) {
      add(P{});
    } else {
      throw std::runtime_error("Plugin '" + nameOf<P>() +
                               "' is required but was not added, and cannot be "
                               "created without arguments");
    }
  }

  template <typename P> bool has() const {
    return stateOf(typeid(P)) == PluginState::Built;
  }

  ecs::World &world() {
    return *m_world;
  }

  void setRunner(Runner runner) {
    m_runner = std::move(runner);
  }

  // Runs every plugin's ready(), then creates the systems.
  void initialize();
  // One frame: advances world.time by deltaTime and runs the systems.
  void update(double deltaTime);
  // Destroys the systems, then cleans the plugins up in reverse order. Safe to
  // call twice; the destructor calls it.
  void shutdown();
  // initialize(), the runner until EngineControl::exitRequested (or whatever
  // the runner watches), then shutdown().
  void run();

private:
  enum class PluginState { Absent, Building, Built };

  template <typename P> static std::string nameOf() {
    return ecs::detail::readableSystemName(typeid(P).name());
  }

  PluginState stateOf(const std::type_index &type) const;
  void addPlugin(const std::type_index &type, std::unique_ptr<IPlugin> plugin);

  // The world is large, so it lives on the heap and an Engine can sit on the
  // stack.
  std::unique_ptr<ecs::World> m_world;
  std::vector<std::unique_ptr<IPlugin>> m_plugins;
  std::unordered_map<std::type_index, PluginState> m_pluginStates;
  Runner m_runner;
  bool m_initialized = false;
};

// A loop without a window: update() at `hz` times a second until the world's
// EngineControl asks to exit. What run() uses when no plugin set a runner.
Engine::Runner fixedRateRunner(double hz = 60.0);

} // namespace componeng::core
