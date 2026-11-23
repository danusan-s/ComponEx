#pragma once

#include "componeng/net/address.hpp"
#include "componeng/net/nat/nat_discovery.hpp"
#include "componeng/net/network.hpp"

#include <functional>
#include <string>
#include <vector>

namespace componeng::net {

// Reaching peers across the internet. Opt-in: create one for a Network before
// it opens, and link ComponEngNetNat. Without it the network is LAN and direct
// addresses only.
//
// STUN tells a socket its public address; it does not introduce peers. Peers
// swap addresses themselves (inviteCode()), then both send to each other at
// about the same time so the routers let the packets in (Network::punch):
// discoverPublicAddress(), show inviteCode(), the host punch()es the joiner's
// address and the joiner connectToInvite()s the host's code. Fails through
// symmetric NATs (see natType()).
//
// Holds a reference to the Network, so keep the Network where it is.
class NatTraversal {
public:
  // Installs itself on the network. If the network is already open that fails,
  // is logged, and every call below does nothing.
  explicit NatTraversal(Network &network);

  bool isActive() const;

  // Asks STUN servers (host:port, resolved in the background; defaults are
  // Google's and Cloudflare's) for this socket's public address. onDiscovery()
  // fires when done; two answering servers also give natType().
  void discoverPublicAddress(std::vector<std::string> servers = {});
  NatDiscovery::State discoveryState() const;
  Address publicAddress() const;
  NatType natType() const;
  // Fires once per discoverPublicAddress(); the bool says whether any server
  // answered.
  void onDiscovery(std::function<void(bool, const NatInfo &)> handler);

  // Empty until the public address is known.
  std::string inviteCode() const;
  // False if `code` is not a valid invite code.
  bool connectToInvite(const std::string &code);

  static std::vector<std::string> defaultStunServers();

private:
  Network &m_network;
};

} // namespace componeng::net
