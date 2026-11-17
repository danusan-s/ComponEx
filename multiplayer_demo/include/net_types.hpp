#pragma once

#include "componeng/core/types.hpp"
#include "componeng/net/archive_math.hpp"

#include <cstdint>
#include <string>

namespace multiplayer_demo {

// Identifies this game on the wire: peers with another value ignore each other.
// Change it whenever PlayerState (or any message) changes shape.
constexpr uint32_t kProtocolId = 0x434D5001;
constexpr uint16_t kDefaultPort = 7777;

// What every peer knows about one player. Each peer owns exactly one of these
// (its own) in a net::SharedObjects set and streams it while it plays.
struct PlayerState {
  componeng::core::Vec3 position{0.0f};
  float yaw = 0.0f; // degrees, same convention as the camera
  float pitch = 0.0f;

  template <typename Ar> void serialize(Ar &ar) {
    ar(position, yaw, pitch);
  }
};

// Set by main() before the world starts.
struct LaunchConfig {
  bool host = true;
  // No arguments given: host if the port is free, otherwise join the instance
  // already hosting it on this machine (so running the demo twice just works).
  bool autoJoinLocal = false;
  std::string address; // "ip:port" to join (when !host)
  uint16_t port = kDefaultPort; // local UDP port; 0 picks any free one
};

} // namespace multiplayer_demo
