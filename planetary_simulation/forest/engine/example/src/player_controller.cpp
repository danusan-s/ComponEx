#include "player_controller.hpp"

#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/input/action_state.hpp"
#include "componeng/input/input_state.hpp"
#include "componeng/utils/logger.hpp"

using namespace componeng;

static constexpr float DEFAULT_MOVE_SPEED = 100.0f;
static constexpr float MOUSE_SENSITIVITY = 0.05f;
static constexpr float PITCH_LIMIT = 89.0f;

static void getCameraVectors(const core::TransformComponent &transform,
                             core::Vec3 &front, core::Vec3 &right,
                             core::Vec3 &up) {
  float cosYaw = cos(core::radians(transform.rotation.y));
  float sinYaw = sin(core::radians(transform.rotation.y));
  float cosPitch = cos(core::radians(transform.rotation.x));
  float sinPitch = sin(core::radians(transform.rotation.x));

  front.x = cosYaw * cosPitch;
  front.y = sinPitch;
  front.z = sinYaw * cosPitch;
  front = normalize(front);

  right = normalize(cross(front, core::Vec3(0.0f, 1.0f, 0.0f)));
  up = normalize(cross(right, front));
}

static void processKeyboardInput(core::TransformComponent &transform,
                                 input::ActionState &actions, float deltaTime,
                                 float speed) {
  float velocity = speed * deltaTime;
  core::Vec3 front, right, up;
  getCameraVectors(transform, front, right, up);

  if (actions.down(input::Action::Sprint))
    velocity *= 2.0f;

  if (actions.down(input::Action::MoveForward))
    transform.position = transform.position + front * velocity;
  if (actions.down(input::Action::MoveBackward))
    transform.position = transform.position - front * velocity;
  if (actions.down(input::Action::MoveLeft))
    transform.position = transform.position - right * velocity;
  if (actions.down(input::Action::MoveRight))
    transform.position = transform.position + right * velocity;
  if (actions.down(input::Action::Jump))
    transform.position =
        transform.position + core::Vec3(0.0f, 1.0f, 0.0f) * velocity;
  if (actions.down(input::Action::Crouch))
    transform.position =
        transform.position - core::Vec3(0.0f, 1.0f, 0.0f) * velocity;
}

static void processMouseInput(core::TransformComponent &transform,
                              input::InputState &input, float sensitivity) {
  transform.rotation.y += input.getMouseDeltaX() * sensitivity;
  transform.rotation.x -= input.getMouseDeltaY() * sensitivity;

  if (transform.rotation.x > PITCH_LIMIT)
    transform.rotation.x = PITCH_LIMIT;
  if (transform.rotation.x < -PITCH_LIMIT)
    transform.rotation.x = -PITCH_LIMIT;
}

void PlayerController::onUpdate(const componeng::ecs::SystemState &state) {
  componeng::input::InputState &input =
      state.world->getResource<input::InputState>();

  componeng::input::ActionState &actions =
      state.world->getResource<input::ActionState>();

  ecs::EntityID mainCameraEntity =
      state.world->getResource<camera::MainCamera>().entity;

  core::TransformComponent &transform =
      state.world->getComponent<core::TransformComponent>(mainCameraEntity);

  processKeyboardInput(transform, actions, state.deltaTime, DEFAULT_MOVE_SPEED);

  processMouseInput(transform, input, MOUSE_SENSITIVITY);
}
