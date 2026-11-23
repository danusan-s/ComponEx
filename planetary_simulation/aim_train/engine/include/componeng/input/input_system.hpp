#pragma once

#include "componeng/ecs/system.hpp"
#include <GLFW/glfw3.h>

namespace componeng::input {

/**
 * @brief PreUpdate system that turns the raw key and mouse state into semantic
 * ActionState for simulation systems to read.
 *
 * Runs once per frame before simulation systems so they can read
 * the current input state without depending on GLFW directly.
 * This decoupling allows for input remapping easily.
 */
class InputSystem final : public ecs::ISystem {
public:
  void onUpdate(const ecs::SystemState &state) override;
};

/**
 * @brief FrameEnd system that keeps this frame's input as "previous", so next
 * frame can tell a press from a hold and measure the mouse delta.
 */
class InputBufferSystem final : public ecs::ISystem {
public:
  void onUpdate(const ecs::SystemState &state) override;
};

} // namespace componeng::input
