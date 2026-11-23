#pragma once

#include "componeng/net/session_types.hpp"
#include "componeng/net/transport.hpp"

namespace componeng::events {

// Emitted by net::EcsBridge. Readable for one frame: the one after
// the network system produced them.

// join() finished: this peer is in the session and connected to all the others.
struct SessionJoinedEvent {};

struct SessionJoinFailedEvent {
  net::JoinFailure reason = net::JoinFailure::Unreachable;
};

struct SessionPeerJoinedEvent {
  net::PeerId peer = net::kNoPeer;
};

struct SessionPeerLeftEvent {
  net::PeerId peer = net::kNoPeer;
  net::DisconnectReason reason = net::DisconnectReason::Remote;
};

// The leader left and `leader` (possibly this peer) took over.
struct SessionLeaderChangedEvent {
  net::PeerId leader = net::kNoPeer;
};

// One per message type registered through Session::registerMessage<T>, from
// peers in the session.
template <typename T> struct SessionMessageEvent {
  net::PeerId from = net::kNoPeer;
  T message;
};

} // namespace componeng::events
