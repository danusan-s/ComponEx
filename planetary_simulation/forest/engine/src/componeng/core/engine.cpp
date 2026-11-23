#include "componeng/core/engine.hpp"

#include "componeng/utils/logger.hpp"

#include <chrono>
#include <thread>

namespace componeng::core {

Engine::Engine() : m_world(std::make_unique<ecs::World>()) {
  m_world->setResource(EngineControl{});
}

Engine::~Engine() {
  shutdown();
}

Engine::PluginState Engine::stateOf(const std::type_index &type) const {
  auto it = m_pluginStates.find(type);
  return it == m_pluginStates.end() ? PluginState::Absent : it->second;
}

void Engine::addPlugin(const std::type_index &type,
                       std::unique_ptr<IPlugin> plugin) {
  const std::string name = ecs::detail::readableSystemName(type.name());
  if (m_initialized) {
    throw std::logic_error("Plugin '" + name +
                           "' added after the engine was initialized");
  }
  if (stateOf(type) != PluginState::Absent) {
    throw std::runtime_error(
        "Plugin '" + name +
        "' is already added (is it part of a plugin group you also use?)");
  }
  LOG_INFO("Building plugin: %s", name.c_str());
  m_pluginStates[type] = PluginState::Building;
  // Anything it requires is built, and listed, before it is.
  plugin->build(*this);
  m_pluginStates[type] = PluginState::Built;
  m_plugins.push_back(std::move(plugin));
}

void Engine::initialize() {
  if (m_initialized) {
    return;
  }
  for (auto &plugin : m_plugins) {
    plugin->ready(*m_world);
  }
  m_world->createSystems();
  m_initialized = true;
}

void Engine::update(double deltaTime) {
  if (!m_initialized) {
    throw std::logic_error("Engine::update before initialize");
  }
  m_world->time += deltaTime;
  m_world->updateSystems(static_cast<float>(deltaTime));
}

void Engine::shutdown() {
  if (!m_initialized) {
    return;
  }
  m_initialized = false;
  m_world->destroySystems();
  for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
    (*it)->cleanup(*m_world);
  }
}

void Engine::run() {
  initialize();
  LOG_INFO("Starting main loop");
  if (m_runner) {
    m_runner(*this);
  } else {
    fixedRateRunner()(*this);
  }
  LOG_INFO("Exiting main loop");
  shutdown();
}

Engine::Runner fixedRateRunner(double hz) {
  return [hz](Engine &engine) {
    using Clock = std::chrono::steady_clock;
    const auto tick = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / hz));
    auto next = Clock::now();
    while (!engine.world().getResource<EngineControl>().exitRequested) {
      engine.update(1.0 / hz);
      next += tick;
      std::this_thread::sleep_until(next);
    }
  };
}

} // namespace componeng::core
