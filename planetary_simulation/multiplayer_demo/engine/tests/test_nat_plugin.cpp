#include "componeng/core/engine.hpp"
#include "componeng/net/nat/nat_plugin.hpp"
#include "componeng/net/nat/nat_traversal.hpp"
#include "componeng/net/net_plugin.hpp"
#include "gtest/gtest.h"

#include <stdexcept>

using namespace componeng;
using namespace componeng::net;

TEST(NatPlugin, RequiresNetworkingAndAddsNatTraversal) {
  core::Engine engine;
  engine.add(NatPlugin{});
  EXPECT_TRUE(engine.has<NetPlugin>()); // pulled in
  EXPECT_TRUE(engine.world().hasResource<Network>());
  EXPECT_TRUE(engine.world().hasResource<NatTraversal>());
  EXPECT_TRUE(engine.world()
                  .getResource<NatTraversal>()
                  .isActive()); // installed before the network opens
}

TEST(NatPlugin, WorksWhenNetPluginWasAddedFirst) {
  core::Engine engine;
  engine.add(NetPlugin{}).add(NatPlugin{});
  EXPECT_TRUE(engine.world().getResource<NatTraversal>().isActive());
}

TEST(NatPlugin, NetworkingAloneHasNoNatTraversal) {
  core::Engine engine;
  engine.add(NetPlugin{});
  EXPECT_FALSE(engine.world().hasResource<NatTraversal>());
}

TEST(NatPlugin, AddingNetPluginAfterItIsAlreadyPulledInThrows) {
  core::Engine engine;
  engine.add(NatPlugin{});
  EXPECT_THROW(engine.add(NetPlugin{}), std::runtime_error);
}
