#include "player_controller.hpp"

#include "local_player.hpp"

#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/input/action_state.hpp"
#include "componeng/input/input_state.hpp"
#include "componeng/physics/collider_component.hpp"
#include "componeng/physics/rigidbody_component.hpp"

#include <cmath>
#include <random>

namespace multiplayer_demo {

using namespace componeng;

namespace {
constexpr float kMouseSensitivity = 0.085f; // degrees per pixel
constexpr float kWalkSpeed = 5.0f;
constexpr float kSprintSpeed = 9.0f;
constexpr float kJumpSpeed = 6.0f;
constexpr float kPitchLimit = 89.0f;
constexpr float kKillHeight = -10.0f;

// Instances start at random spots so two of them do not spawn inside each other.
core::Vec3 spawnPoint() {
  static std::mt19937 gen(std::random_device{}());
  std::uniform_real_distribution<float> spread(-4.0f, 4.0f);
  return core::Vec3(spread(gen), kBodyHeight * 0.5f + 0.5f, spread(gen));
}
} // namespace

void PlayerController::onCreate(const ecs::SystemState &state) {
  m_player = state.world->createEntity();
  state.world->addComponents(
      m_player,
      core::TransformComponent{
          .position = spawnPoint(),
          .scale = core::Vec3(kBodyDepth, kBodyHeight, kBodyWidth)},
      physics::RigidBodyComponent{
          .type = physics::RigidBodyComponent::Type::Dynamic,
          .velocity = core::Vec3(0.0f),
          .mass = 80.0f,
          .restitution = 0.0f},
      physics::ColliderComponent{.type = physics::ColliderType::Box},
      LocalPlayerComponent{});
}

void PlayerController::onUpdate(const ecs::SystemState &state) {
  ecs::World &world = *state.world;
  auto &input = world.getResource<input::InputState>();
  auto &actions = world.getResource<input::ActionState>();
  auto &mainCam = world.getResource<camera::MainCamera>();

  auto &transform = world.getComponent<core::TransformComponent>(m_player);
  auto &body = world.getComponent<physics::RigidBodyComponent>(m_player);
  auto &look = world.getComponent<LocalPlayerComponent>(m_player);

  // Mouse look.
  look.yaw += input.getMouseDeltaX() * kMouseSensitivity;
  look.pitch -= input.getMouseDeltaY() * kMouseSensitivity;
  look.pitch = std::fmax(-kPitchLimit, std::fmin(kPitchLimit, look.pitch));

  // Walk: horizontal velocity comes straight from input, vertical is left to
  // gravity and the floor collision.
  const float yaw = core::radians(look.yaw);
  const core::Vec3 forward(std::cos(yaw), 0.0f, std::sin(yaw));
  const core::Vec3 right(-std::sin(yaw), 0.0f, std::cos(yaw));

  core::Vec3 wish(0.0f);
  if (actions.down(input::Action::MoveForward))
    wish += forward;
  if (actions.down(input::Action::MoveBackward))
    wish -= forward;
  if (actions.down(input::Action::MoveRight))
    wish += right;
  if (actions.down(input::Action::MoveLeft))
    wish -= right;
  if (core::dot(wish, wish) > 0.0f)
    wish = core::normalize(wish);

  const float speed =
      actions.down(input::Action::Sprint) ? kSprintSpeed : kWalkSpeed;
  body.velocity.x = wish.x * speed;
  body.velocity.z = wish.z * speed;

  // Resting on the floor leaves vertical velocity near zero.
  if (actions.pressed(input::Action::Jump) && std::fabs(body.velocity.y) < 0.5f)
    body.velocity.y = kJumpSpeed;

  // Fell off the edge: back to a spawn point.
  if (transform.position.y < kKillHeight) {
    transform.position = spawnPoint();
    body.velocity = core::Vec3(0.0f);
  }

  // First person camera at eye height.
  auto &camTransform =
      world.getComponent<core::TransformComponent>(mainCam.entity);
  camTransform.position = transform.position + core::Vec3(0.0f, kEyeHeight, 0.0f);
  camTransform.rotation = core::Vec3(look.pitch, look.yaw, 0.0f);
}

} // namespace multiplayer_demo
