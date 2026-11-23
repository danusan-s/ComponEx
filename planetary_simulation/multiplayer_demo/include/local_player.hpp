#pragma once

namespace multiplayer_demo {

// Marks the entity this instance controls. Its transform is the player's body;
// the look angles live here because the body itself is never rotated (its box
// collider is axis aligned).
struct LocalPlayerComponent {
  static constexpr const char *component_name = "LocalPlayerComponent";
  float yaw = 0.0f;   // degrees; 0 looks along +X
  float pitch = 0.0f; // degrees
};

// Body size shared by the local collider and the remote avatars.
constexpr float kBodyWidth = 0.6f;
constexpr float kBodyHeight = 1.8f;
constexpr float kBodyDepth = 0.4f;
constexpr float kEyeHeight = 0.7f; // above the body's centre

} // namespace multiplayer_demo
