#pragma once

#include "componeng/core/plugin.hpp"

namespace componeng::net {

// Internet play for a NetPlugin game: adds the NatTraversal resource (STUN,
// public address, invite codes). Requires NetPlugin, and links ComponEngNetNat.
// It is built before the game can open the network, which is when it has to
// be.
class NatPlugin final : public core::IPlugin {
public:
  void build(core::Engine &engine) override;
};

} // namespace componeng::net
