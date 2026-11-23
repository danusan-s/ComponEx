#pragma once

namespace componeng::ecs {
class World;
}

namespace componeng::core {

class Engine;

/**
 * @brief A bundle of registrations that adds one feature to the engine.
 *
 * Plugins describe what exists: components, resources, systems and the order
 * between them. They have no per-frame hook; per-frame work is a system the
 * plugin registers. Configuration is plain data on the plugin object.
 *
 *   engine.add(DefaultPlugins{}).add(MyGame{}).run();
 *
 * The three calls happen in different phases, always in the order the plugins
 * were added (a plugin required by another counts as added just before it):
 *
 *   build    when added: register components, resources and systems. Cheap, and
 *            nothing is opened or loaded yet.
 *   ready    once every plugin is built, before the first frame: open windows
 *            and sockets, load assets, create the starting entities. Systems
 *            are created right after.
 *   cleanup  after the systems are destroyed, in reverse order, so a plugin is
 *            cleaned up before the ones it required.
 */
class IPlugin {
public:
  virtual ~IPlugin() = default;
  virtual void build(Engine &engine) = 0;
  virtual void ready(ecs::World &) {
  }
  virtual void cleanup(ecs::World &) {
  }
};

} // namespace componeng::core
