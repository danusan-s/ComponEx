#pragma once

#include <cstdint>

namespace componeng::net {

// The host is 0 and each joiner gets the next number; numbers are not reused
// while the session lives.
using PeerId = uint16_t;
constexpr PeerId kNoPeer = 0xFFFF;

// Why Session::join() did not end with the peer in the session.
enum class JoinFailure : uint8_t {
  Full,      // the session has its maximum number of peers
  NotLeader, // that peer is in the session but is not the one to join through
  WrongSession, // the peer runs another session than the one asked for
  Unreachable,  // nobody answered at that address
  MeshFailed,   // joined, but could not reach one of the other peers directly
  Timeout       // took too long
};

} // namespace componeng::net
