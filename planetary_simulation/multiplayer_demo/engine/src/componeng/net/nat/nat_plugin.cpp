#include "componeng/net/nat/nat_plugin.hpp"

#include "componeng/core/engine.hpp"
#include "componeng/net/nat/nat_traversal.hpp"
#include "componeng/net/net_plugin.hpp"

namespace componeng::net {

void NatPlugin::build(core::Engine &engine) {
  engine.require<NetPlugin>();
  ecs::World &world = engine.world();
  world.setResource<NatTraversal>(NatTraversal(world.getResource<Network>()));
}

} // namespace componeng::net
