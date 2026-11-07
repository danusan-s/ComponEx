#include "player_system.hpp"

#include "components.hpp"
#include "forest_world.hpp"

#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/input/action_state.hpp"
#include "componeng/input/input_state.hpp"

#include <algorithm>
#include <cmath>

namespace forest {

using componeng::core::Vec3;

namespace {

constexpr float kRadius = 0.36f;
constexpr float kWalk = 4.2f, kSprint = 7.4f, kCrouch = 1.9f;
constexpr float kJump = 7.0f, kGravity = 20.0f;
constexpr float kMinSlopeNormalY = 0.62f;  // ~52 degrees: steeper cannot be climbed
constexpr float kStepUp = 0.45f;
constexpr float kEyeStanding = 1.68f, kEyeCrouched = 1.05f;
constexpr float kMouseSensitivity = 0.085f;

// Highest surface the feet can stand on at (x, z): the terrain, or the top of
// a flat obstacle (a stump) the feet are already roughly level with.
float groundAt(const ForestWorld &fw, float x, float z, float feetY) {
  float g = fw.terrain.heightAt(x, z);
  fw.obstacles.query(x, z, 0.0f, [&](const Obstacle &o) {
    if (!o.walkable) return;
    float dx = x - o.x, dz = z - o.z;
    float top = o.baseY + o.top;
    if (dx * dx + dz * dz < o.r * o.r && feetY >= top - kStepUp) g = std::max(g, top);
  });
  return g;
}

// Pushes (x, z) out of every obstacle it overlaps, and removes the part of the
// velocity that points into it so the player slides along trunks.
void resolveObstacles(const ForestWorld &fw, float &x, float &z, float feetY, Vec3 &vel) {
  for (int pass = 0; pass < 3; ++pass) {
    bool moved = false;
    fw.obstacles.query(x, z, kRadius, [&](const Obstacle &o) {
      // Feet above the top: walk over (or onto) it.
      if (feetY >= o.baseY + o.top - 0.12f) return;
      float dx = x - o.x, dz = z - o.z;
      float minD = o.r + kRadius;
      float d2 = dx * dx + dz * dz;
      if (d2 >= minD * minD) return;
      float d = std::sqrt(d2);
      float nx = d > 1e-5f ? dx / d : 1.0f, nz = d > 1e-5f ? dz / d : 0.0f;
      x += nx * (minD - d);
      z += nz * (minD - d);
      float into = vel.x * nx + vel.z * nz;
      if (into < 0.0f) {
        vel.x -= into * nx;
        vel.z -= into * nz;
      }
      moved = true;
    });
    if (!moved) break;
  }
}

} // namespace

void PlayerSystem::onUpdate(const componeng::ecs::SystemState &state) {
  using namespace componeng;
  ecs::World &world = *state.world;
  const float dt = std::min(state.deltaTime, 0.05f);  // the first frame includes loading time

  const ForestWorld &fw = world.getResource<ForestWorld>();
  auto &input = world.getResource<input::InputState>();
  auto &actions = world.getResource<input::ActionState>();
  ecs::EntityID camEntity = world.getResource<camera::MainCamera>().entity;
  core::TransformComponent &camTf = world.getComponent<core::TransformComponent>(camEntity);

  world.query<core::TransformComponent, PlayerComponent>().each(
      [&](core::TransformComponent &tf, PlayerComponent &p) {
        // --- Look --------------------------------------------------------
        p.yaw += input.getMouseDeltaX() * kMouseSensitivity;
        p.pitch -= input.getMouseDeltaY() * kMouseSensitivity;
        p.pitch = std::clamp(p.pitch, -88.0f, 88.0f);

        float yawRad = core::radians(p.yaw);
        Vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
        Vec3 right(-forward.z, 0.0f, forward.x);

        // --- Wish velocity -------------------------------------------------
        Vec3 wish(0.0f);
        if (actions.down(input::Action::MoveForward)) wish = wish + forward;
        if (actions.down(input::Action::MoveBackward)) wish = wish - forward;
        if (actions.down(input::Action::MoveRight)) wish = wish + right;
        if (actions.down(input::Action::MoveLeft)) wish = wish - right;
        float len = std::sqrt(wish.x * wish.x + wish.z * wish.z);
        if (len > 0.0f) wish = wish * (1.0f / len);

        bool crouching = actions.down(input::Action::Crouch);
        float speed = crouching ? kCrouch : (actions.down(input::Action::Sprint) ? kSprint : kWalk);
        float control = p.onGround ? 12.0f : 2.5f;  // little steering in the air
        float k = 1.0f - std::exp(-control * dt);
        p.velocity.x += (wish.x * speed - p.velocity.x) * k;
        p.velocity.z += (wish.z * speed - p.velocity.z) * k;

        if (p.onGround && actions.pressed(input::Action::Jump)) {
          p.velocity.y = kJump;
          p.onGround = false;
        }
        p.velocity.y -= kGravity * dt;

        // --- Horizontal move: slope limit, then obstacles ------------------
        float x = tf.position.x, z = tf.position.z;
        float nx = x + p.velocity.x * dt, nz = z + p.velocity.z * dt;
        float step = std::sqrt((nx - x) * (nx - x) + (nz - z) * (nz - z));
        if (p.onGround && step > 1e-5f) {
          // Too steep to climb? Try each axis alone so the player slides along
          // the slope instead of stopping dead.
          auto climbable = [&](float tx, float tz) {
            float d = std::sqrt((tx - x) * (tx - x) + (tz - z) * (tz - z));
            if (d < 1e-5f) return true;
            Vec3 n = fw.terrain.normalAt(tx, tz);
            float rise = fw.terrain.heightAt(tx, tz) - tf.position.y;
            return n.y >= kMinSlopeNormalY || rise <= 0.0f;
          };
          if (!climbable(nx, nz)) {
            if (climbable(nx, z)) nz = z;
            else if (climbable(x, nz)) nx = x;
            else { nx = x; nz = z; }
            p.velocity.x = (nx - x) / dt;
            p.velocity.z = (nz - z) / dt;
          }
        }
        resolveObstacles(fw, nx, nz, tf.position.y, p.velocity);

        float lim = fw.terrain.half() - 12.0f;
        nx = std::clamp(nx, -lim, lim);
        nz = std::clamp(nz, -lim, lim);

        // --- Vertical ----------------------------------------------------------
        float y = tf.position.y + p.velocity.y * dt;
        float ground = groundAt(fw, nx, nz, tf.position.y);
        if (y <= ground) {
          y = ground;
          if (p.velocity.y < 0.0f) p.velocity.y = 0.0f;
          p.onGround = true;
        } else if (p.onGround && p.velocity.y <= 0.0f && y - ground < 0.4f) {
          y = ground;  // walking downhill: stay on the surface instead of hopping
        } else {
          p.onGround = false;
        }
        tf.position = Vec3(nx, y, nz);

        // --- Camera ------------------------------------------------------------
        float eyeTarget = crouching ? kEyeCrouched : kEyeStanding;
        p.eye += (eyeTarget - p.eye) * (1.0f - std::exp(-10.0f * dt));

        if (p.onGround && std::fabs(y - p.smoothY) < 1.5f)
          p.smoothY += (y - p.smoothY) * (1.0f - std::exp(-20.0f * dt));
        else
          p.smoothY = y;

        float planar = std::sqrt(p.velocity.x * p.velocity.x + p.velocity.z * p.velocity.z);
        if (p.onGround) p.bobPhase += planar * dt * 2.1f;
        float bob = std::sin(p.bobPhase) * 0.032f * std::min(planar / kWalk, 1.4f);

        camTf.position = Vec3(nx, p.smoothY + p.eye + bob, nz);
        camTf.rotation = Vec3(p.pitch, p.yaw, 0.0f);
      });
}

} // namespace forest
