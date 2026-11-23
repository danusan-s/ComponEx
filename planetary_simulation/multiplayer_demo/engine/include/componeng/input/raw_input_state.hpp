#pragma once

namespace componeng::input {

/**
 * @brief Snapshot of current input state (keyboard and mouse).
 *
 * Window updates input state and InputSystem consumes to convert into input
 * components for the engine to use.
 */
struct RawInputState {
  bool keys[1024] = {false};
  bool mouseButtons[8] = {false};
  float mouseX = 0.0f;
  float mouseY = 0.0f;
};

} // namespace componeng::input
