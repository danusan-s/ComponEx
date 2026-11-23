#include "componeng/net/net_plugin.hpp"

#include "componeng/core/engine.hpp"
#include "componeng/ecs/system.hpp"
#include "componeng/net/ecs_bridge.hpp"
#include "componeng/net/lan_discovery.hpp"
#include "componeng/net/network.hpp"
#include "componeng/net/session.hpp"

namespace componeng::net {
namespace {

// Pumps the network, then what depends on it.
class NetworkSystem final : public ecs::ISystem {
public:
  void onUpdate(const ecs::SystemState &state) override {
    ecs::World &world = *state.world;
    const double now = world.time;
    world.getResource<Network>().update(now);
    // After the network: the session reacts to what it just delivered.
    world.getResource<Session>().update(now);
    world.getResource<LanBrowser>().update(now);
  }
};

} // namespace

void NetPlugin::build(core::Engine &engine) {
  ecs::World &world = engine.world();
  world.setResource<Network>(Network());
  world.setResource<Session>(Session(world.getResource<Network>()));
  world.setResource<LanBrowser>(LanBrowser());
  world.setResource<EcsBridge>(EcsBridge());
  world.registerSystem<NetworkSystem>(ecs::SystemGroup::PreUpdate);
}

void NetPlugin::ready(ecs::World &world) {
  EcsBridge &bridge = world.getResource<EcsBridge>();
  bridge.bindWorld(&world);
  bridge.attach(world.getResource<Network>());
  bridge.attach(world.getResource<Session>());
}

void NetPlugin::cleanup(ecs::World &world) {
  // The session says goodbye through the network, so it leaves first.
  world.getResource<Session>().leave();
  world.getResource<LanBrowser>().stop();
  world.getResource<Network>().close();
  // The world is going away; the bridge may not outlive it.
  world.getResource<EcsBridge>().bindWorld(nullptr);
}

} // namespace componeng::net
