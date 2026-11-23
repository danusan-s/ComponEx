#pragma once

#include "componeng/core/plugin.hpp"

namespace componeng::input {

// Input state and actions: the InputState and ActionState resources, the system
// that turns keys into actions, and the one that rolls the frame over. A window
// (WindowPlugin) fills the raw state; a test or a replay can fill it too.
class InputPlugin final : public core::IPlugin {
public:
  void build(core::Engine &engine) override;
};

} // namespace componeng::input
