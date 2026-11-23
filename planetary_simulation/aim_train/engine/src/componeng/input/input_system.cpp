#include "componeng/input/input_system.hpp"

#include "componeng/ecs/world.hpp"
#include "componeng/input/action_state.hpp"
#include "componeng/input/input_state.hpp"
#include <GLFW/glfw3.h>

namespace componeng::input {

constexpr auto FORWARD_KEY = GLFW_KEY_W;
constexpr auto BACKWARD_KEY = GLFW_KEY_S;
constexpr auto LEFT_KEY = GLFW_KEY_A;
constexpr auto RIGHT_KEY = GLFW_KEY_D;
constexpr auto JUMP_KEY = GLFW_KEY_SPACE;
constexpr auto CROUCH_KEY = GLFW_KEY_LEFT_SHIFT;
constexpr auto SPRINT_KEY = GLFW_KEY_LEFT_CONTROL;
constexpr auto ATTACK_KEY = GLFW_MOUSE_BUTTON_LEFT;
constexpr auto AIM_KEY = GLFW_MOUSE_BUTTON_RIGHT;

void InputSystem::onUpdate(const ecs::SystemState &state) {
  auto &inputState = state.world->getResource<InputState>();
  auto &actionState = state.world->getResource<ActionState>();

  actionState.current[static_cast<size_t>(Action::MoveForward)] =
      inputState.isKeyDown(FORWARD_KEY);
  actionState.current[static_cast<size_t>(Action::MoveBackward)] =
      inputState.isKeyDown(BACKWARD_KEY);
  actionState.current[static_cast<size_t>(Action::MoveLeft)] =
      inputState.isKeyDown(LEFT_KEY);
  actionState.current[static_cast<size_t>(Action::MoveRight)] =
      inputState.isKeyDown(RIGHT_KEY);
  actionState.current[static_cast<size_t>(Action::Jump)] =
      inputState.isKeyDown(JUMP_KEY);
  actionState.current[static_cast<size_t>(Action::Crouch)] =
      inputState.isKeyDown(CROUCH_KEY);
  actionState.current[static_cast<size_t>(Action::Sprint)] =
      inputState.isKeyDown(SPRINT_KEY);
  actionState.current[static_cast<size_t>(Action::Attack)] =
      inputState.isMouseButtonPressed(ATTACK_KEY);
  actionState.current[static_cast<size_t>(Action::Aim)] =
      inputState.isMouseButtonPressed(AIM_KEY);
}

void InputBufferSystem::onUpdate(const ecs::SystemState &state) {
  auto &inputState = state.world->getResource<InputState>();
  auto &actionState = state.world->getResource<ActionState>();
  inputState.previous_state = inputState.current_state;
  actionState.swapBuffers();
}

} // namespace componeng::input
