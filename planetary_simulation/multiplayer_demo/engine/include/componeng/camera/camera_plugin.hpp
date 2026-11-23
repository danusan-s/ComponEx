#pragma once

#include "componeng/core/plugin.hpp"

namespace componeng::camera {

// The camera component and system. If nothing set a MainCamera by the time
// every plugin is ready, it makes a default camera entity, so a game that
// sets its own in its own ready() just replaces it.
class CameraPlugin final : public core::IPlugin {
public:
  void build(core::Engine &engine) override;
  void ready(ecs::World &world) override;
};

} // namespace componeng::camera
