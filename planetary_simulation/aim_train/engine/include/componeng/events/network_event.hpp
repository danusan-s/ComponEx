#pragma once

#include "componeng/net/address.hpp"
#include "componeng/net/transport.hpp"

namespace componeng::events {

// Emitted by net::EcsBridge. Like all events, readable for one frame: the one
// after the network system produced them.

struct PeerConnectedEvent {
  net::Address peer;
};

struct PeerDisconnectedEvent {
  net::Address peer;
  net::DisconnectReason reason = net::DisconnectReason::Local;
};

// A Network::connect() that never completed.
struct ConnectFailedEvent {
  net::Address peer;
  net::DisconnectReason reason = net::DisconnectReason::ConnectTimeout;
};

// One per registered message type.
template <typename T> struct MessageReceivedEvent {
  net::Address from;
  T message;
};

} // namespace componeng::events
