#pragma once

#include "componeng/core/plugin.hpp"

namespace componeng::physics {

// Rigid bodies and colliders, the PhysicsConfig resource and the PhysicsSystem.
class PhysicsPlugin final : public core::IPlugin {
public:
  void build(core::Engine &engine) override;
};

} // namespace componeng::physics
