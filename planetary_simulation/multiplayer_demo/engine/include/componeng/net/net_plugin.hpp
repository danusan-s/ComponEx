#pragma once

#include "componeng/core/plugin.hpp"

namespace componeng::net {

// Opts a game in to networking: the Network, Session, LanBrowser and EcsBridge
// resources, and the NetworkSystem that updates them once a frame ahead of the
// systems that read the results. Nothing is opened until the game calls
// Network::open(). On cleanup the session leaves and the network closes. A game
// that never adds it pays nothing.
//
// Internet play is a further opt-in: see nat/nat_plugin.hpp.
class NetPlugin final : public core::IPlugin {
public:
  void build(core::Engine &engine) override;
  void ready(ecs::World &world) override;
  void cleanup(ecs::World &world) override;
};

} // namespace componeng::net
