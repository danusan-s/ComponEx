#include "player_controller.hpp"

#include <imgui/imgui.h>

#include "componeng/components/collider_component.hpp"
#include "componeng/components/rigidbody_component.hpp"
#include "componeng/components/transform_component.hpp"
#include "componeng/core/debug_ui.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/resources/action_state.hpp"
#include "componeng/resources/input_state.hpp"
#include "componeng/resources/main_camera.hpp"

namespace aim_train {

using namespace componeng;

static void getCameraVectors(const components::TransformComponent &t,
                             core::Vec3 &front, core::Vec3 &right) {
  float cp = cos(core::radians(t.rotation.x));
  float sp = sin(core::radians(t.rotation.x));
  float cy = cos(core::radians(t.rotation.y));
  float sy = sin(core::radians(t.rotation.y));
  core::Vec3 look = core::normalize(core::Vec3(cy * cp, sp, sy * cp));
  right = core::normalize(core::cross(look, core::Vec3(0.0f, 1.0f, 0.0f)));
  front = -core::normalize(core::cross(right, core::Vec3(0.0f, 1.0f, 0.0f)));
}

void PlayerController::onCreate(const ecs::SystemState &state) {
  ecs::EntityID player = state.world->createEntity();
  state.world->addComponents(
      player,
      components::TransformComponent{.position = core::Vec3(0, 1.5f, 0),
                                     .scale = core::Vec3(0.5f, 1.0f, 0.5f)},
      components::RigidBodyComponent{
          .type = components::RigidBodyComponent::Type::Dynamic,
          .velocity = core::Vec3(0, 0, 0),
          .mass = 100.0f,
          .restitution = 0.0f},
      components::ColliderComponent{.type = components::ColliderType::Box});

  m_playerEntity = player;
}

void PlayerController::onUpdate(const ecs::SystemState &state) {
  auto &input = state.world->get_resource<resources::InputState>();
  auto &actions = state.world->get_resource<resources::ActionState>();
  auto &mainCam = state.world->get_resource<resources::MainCamera>();
  auto &playerTransform =
      state.world->getComponent<components::TransformComponent>(m_playerEntity);
  auto &cameraTransform =
      state.world->getComponent<components::TransformComponent>(mainCam.entity);

  // Mouse look
  playerTransform.rotation.y += input.getMouseDeltaX() * m_sensitivity / 10.0f;
  playerTransform.rotation.x -= input.getMouseDeltaY() * m_sensitivity / 10.0f;

  constexpr float PITCH_LIMIT = 89.0f;
  if (playerTransform.rotation.x > PITCH_LIMIT)
    playerTransform.rotation.x = PITCH_LIMIT;
  if (playerTransform.rotation.x < -PITCH_LIMIT)
    playerTransform.rotation.x = -PITCH_LIMIT;

  // Movement
  float speed = 10.0f * state.deltaTime;
  if (actions.down(resources::Action::Sprint))
    speed *= 2.0f;

  core::Vec3 front, right;
  getCameraVectors(playerTransform, front, right);

  if (actions.down(resources::Action::MoveForward))
    playerTransform.position += front * speed;
  if (actions.down(resources::Action::MoveBackward))
    playerTransform.position -= front * speed;
  if (actions.down(resources::Action::MoveLeft))
    playerTransform.position -= right * speed;
  if (actions.down(resources::Action::MoveRight))
    playerTransform.position += right * speed;

  cameraTransform.position =
      playerTransform.position + core::Vec3(0.0f, 1.0f, 0.0f);
  cameraTransform.rotation = playerTransform.rotation;

  ImGui::InputFloat("Sensitivity", &m_sensitivity, 0.01f, 0.1f, "%.3f");
}

} // namespace aim_train
