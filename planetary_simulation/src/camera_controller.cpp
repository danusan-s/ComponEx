#include "camera_controller.hpp"

#include <imgui/imgui.h>

#include "componeng/camera/main_camera.hpp"
#include "componeng/core/debug_ui.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/input/action_state.hpp"
#include "componeng/input/input_state.hpp"

namespace planetary_sim {

using namespace componeng;

static void getCameraVectors(const core::TransformComponent &t,
                             core::Vec3 &front, core::Vec3 &right) {
  float cp = cos(core::radians(t.rotation.x));
  float sp = sin(core::radians(t.rotation.x));
  float cy = cos(core::radians(t.rotation.y));
  float sy = sin(core::radians(t.rotation.y));
  core::Vec3 look = core::normalize(core::Vec3(cy * cp, sp, sy * cp));
  right = core::normalize(core::cross(look, core::Vec3(0.0f, 1.0f, 0.0f)));
  front = -core::normalize(core::cross(right, core::Vec3(0.0f, 1.0f, 0.0f)));
}

void CameraController::onUpdate(const ecs::SystemState &state) {
  auto &input = state.world->getResource<input::InputState>();
  auto &actions = state.world->getResource<input::ActionState>();
  auto &mainCam = state.world->getResource<camera::MainCamera>();
  auto &t =
      state.world->getComponent<core::TransformComponent>(mainCam.entity);

  t.rotation.y += input.getMouseDeltaX() * m_sensitivity / 10.0f;
  t.rotation.x -= input.getMouseDeltaY() * m_sensitivity / 10.0f;

  constexpr float PITCH_LIMIT = 89.0f;
  if (t.rotation.x > PITCH_LIMIT)
    t.rotation.x = PITCH_LIMIT;
  if (t.rotation.x < -PITCH_LIMIT)
    t.rotation.x = -PITCH_LIMIT;

  float speed = m_moveSpeed * state.deltaTime;
  if (actions.down(input::Action::Sprint))
    speed *= 2.0f;

  core::Vec3 front, right;
  getCameraVectors(t, front, right);

  if (actions.down(input::Action::MoveForward))
    t.position += front * speed;
  if (actions.down(input::Action::MoveBackward))
    t.position -= front * speed;
  if (actions.down(input::Action::MoveLeft))
    t.position -= right * speed;
  if (actions.down(input::Action::MoveRight))
    t.position += right * speed;
  if (actions.down(input::Action::Jump))
    t.position.y += speed;
  if (actions.down(input::Action::Crouch))
    t.position.y -= speed;

  core::DebugUI::addValue("Planets", 5.0f);

  ImGui::InputFloat("Sensitivity", &m_sensitivity, 0.01f, 0.1f, "%.3f");
  ImGui::InputFloat("Move Speed", &m_moveSpeed, 0.1f, 1.0f, "%.1f");
}

} // namespace planetary_sim
