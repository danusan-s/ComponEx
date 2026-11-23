#include "player_system.hpp"

#include "components.hpp"
#include "forest_config.hpp"
#include "forest_world.hpp"
#include "water.hpp"

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
constexpr float kSlideSpeed = 4.5f;        // m/s off the top of a trunk or log
// In the air the feet may be this far below an obstacle's top and still get up
// onto it (a mantle), instead of being shoved off its side.
constexpr float kAirStepUp = 0.6f;
// How fast an overlapping obstacle may push the player out, so landing or
// jumping into one slides the player off over a few frames instead of
// teleporting them out of it.
constexpr float kPushSpeed = 7.0f;
constexpr float kCoyoteTime = 0.12f;       // jump still works this long after leaving the ground
constexpr float kEyeStanding = 1.68f, kEyeCrouched = 1.05f;
constexpr float kMouseSensitivity = 0.085f;

// Swimming. The feet start swimming once this far under the surface, and stop
// (back to walking) only when they touch the ground where the water is shallower
// than kWadeDepth. The gap between the two keeps the mode from flickering at the
// shore.
constexpr float kSwimDepth = 1.1f, kWadeDepth = 0.8f;
constexpr float kSwim = 2.8f, kSwimSprint = 4.6f;
constexpr float kSwimControl = 4.0f;       // water is heavy: slow to speed up and slow down
constexpr float kEyeSwim = 0.6f;           // eye above the body's origin while swimming
constexpr float kHeadAboveWater = 0.1f;    // the eye may rise this far above the surface, no further
constexpr float kSwimBuoyancy = 3.0f;      // 1/s: how fast an idle swimmer floats to the surface

// What the feet stand on at (x, z).
struct Ground {
  float y;
  // Set when that is the top of an obstacle that is not flat enough to stay on
  // (a rock, a trunk): the player slides off, away from (slideX, slideZ).
  bool slide = false;
  float slideX = 0.0f, slideZ = 0.0f;
};

// Highest surface the feet can stand on at (x, z): the terrain, or the top of
// an obstacle the feet are already roughly level with (a stump to stay on, a
// rock to stand on, a trunk or log to land on and slide off).
Ground groundAt(const ForestWorld &fw, float x, float z, float feetY, float stepUp) {
  Ground g{fw.terrain.heightAt(x, z)};
  fw.obstacles.query(x, z, 0.0f, [&](const Obstacle &o) {
    float dx = x - o.x, dz = z - o.z;
    float top = o.baseY + o.top;
    if (dx * dx + dz * dz < o.r * o.r && feetY >= top - stepUp && top > g.y) {
      g.y = top;
      g.slide = !o.walkable;
      g.slideX = o.x;
      g.slideZ = o.z;
    }
  });
  return g;
}

// Pushes (x, z) out of every obstacle it overlaps, and removes the part of the
// velocity that points into it so the player slides along trunks.
void resolveObstacles(const ForestWorld &fw, float &x, float &z, float feetY, Vec3 &vel,
                      float reach, float maxPush) {
  for (int pass = 0; pass < 3; ++pass) {
    bool moved = false;
    fw.obstacles.query(x, z, kRadius, [&](const Obstacle &o) {
      // Feet above the top: walk over (or onto) it.
      if (feetY >= o.baseY + o.top - reach) return;
      float dx = x - o.x, dz = z - o.z;
      float minD = o.r + kRadius;
      float d2 = dx * dx + dz * dz;
      if (d2 >= minD * minD) return;
      float d = std::sqrt(d2);
      float nx = d > 1e-5f ? dx / d : 1.0f, nz = d > 1e-5f ? dz / d : 0.0f;
      float push = std::min(minD - d, maxPush);
      x += nx * push;
      z += nz * push;
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

// One frame of swimming: the player moves where they look (forward/back along
// the view direction, strafing level), Jump rises and Crouch dives. The eye is
// capped just above the surface, so looking up while at the surface does not
// lift the player out of the water: it swims along the surface instead. Touching
// the ground in shallow water hands control back to walking.
void swimStep(const ForestWorld &fw, PlayerComponent &p, componeng::core::TransformComponent &tf,
              componeng::core::TransformComponent &camTf, const componeng::input::ActionState &actions,
              Vec3 forward, Vec3 right, float dt, float t) {
  namespace input = componeng::input;

  float f = (actions.down(input::Action::MoveForward) ? 1.0f : 0.0f) -
            (actions.down(input::Action::MoveBackward) ? 1.0f : 0.0f);
  float s = (actions.down(input::Action::MoveRight) ? 1.0f : 0.0f) -
            (actions.down(input::Action::MoveLeft) ? 1.0f : 0.0f);
  float vert = (actions.down(input::Action::Jump) ? 1.0f : 0.0f) -
               (actions.down(input::Action::Crouch) ? 1.0f : 0.0f);

  float pitchRad = componeng::core::radians(p.pitch);
  float cp = std::cos(pitchRad), sp = std::sin(pitchRad);

  Vec3 flat = forward * f + right * s;  // level motion at full strength
  float flatLen = std::sqrt(flat.x * flat.x + flat.z * flat.z);
  if (flatLen > 1.0f) flat = flat * (1.0f / flatLen);

  Vec3 wish = forward * (cp * f) + right * s;  // look-directed motion
  wish.y = sp * f + vert;

  float surface = waterSurfaceHeight(tf.position.x, tf.position.z, t);
  float maxY = surface + kHeadAboveWater - kEyeSwim;
  bool atSurface = tf.position.y >= maxY - 0.08f;
  if (atSurface && wish.y > 0.0f) {
    // The view points up, but there is nowhere to go: keep the full speed along
    // the surface instead of crawling because the look direction is steep.
    wish = flat;
  }
  float wl = std::sqrt(wish.x * wish.x + wish.y * wish.y + wish.z * wish.z);
  if (wl > 1.0f) wish = wish * (1.0f / wl);

  float speed = actions.down(input::Action::Sprint) ? kSwimSprint : kSwim;
  Vec3 target = wish * speed;
  if (wl < 1e-4f) target.y = std::clamp((maxY - tf.position.y) * kSwimBuoyancy, -1.2f, 1.2f);

  float k = 1.0f - std::exp(-kSwimControl * dt);
  p.velocity.x += (target.x - p.velocity.x) * k;
  p.velocity.y += (target.y - p.velocity.y) * k;
  p.velocity.z += (target.z - p.velocity.z) * k;

  float nx = tf.position.x + p.velocity.x * dt, nz = tf.position.z + p.velocity.z * dt;
  resolveObstacles(fw, nx, nz, tf.position.y, p.velocity, 0.12f, kPushSpeed * dt);
  float lim = fw.terrain.half() - 12.0f;
  nx = std::clamp(nx, -lim, lim);
  nz = std::clamp(nz, -lim, lim);

  // Ceiling (the surface where the player ended up), then floor.
  float y = tf.position.y + p.velocity.y * dt;
  float newSurface = waterSurfaceHeight(nx, nz, t);
  float newMaxY = newSurface + kHeadAboveWater - kEyeSwim;
  if (y > newMaxY) {
    y = newMaxY;
    if (p.velocity.y > 0.0f) p.velocity.y = 0.0f;
  }
  Ground g = groundAt(fw, nx, nz, tf.position.y, kAirStepUp);
  bool touching = false;
  if (y <= g.y + 1e-4f) {
    y = g.y;
    if (p.velocity.y < 0.0f) p.velocity.y = 0.0f;
    touching = true;
  }
  tf.position = Vec3(nx, y, nz);

  if (touching && newSurface - g.y < kWadeDepth) {
    // Feet down in shallow water: wade.
    p.swimming = false;
    p.onGround = true;
    p.velocity.y = 0.0f;
    p.coyote = kCoyoteTime;
  }

  p.eye += (kEyeSwim - p.eye) * (1.0f - std::exp(-10.0f * dt));
  p.smoothY = y;
  camTf.position = Vec3(nx, y + p.eye, nz);
  camTf.rotation = Vec3(p.pitch, p.yaw, 0.0f);
}

} // namespace

void PlayerSystem::onUpdate(const componeng::ecs::SystemState &state) {
  using namespace componeng;
  ecs::World &world = *state.world;
  const float dt = std::min(state.deltaTime, 0.05f);  // the first frame includes loading time
  const float t = float(world.time);                  // for the water's surface

  const ForestWorld &fw = world.getResource<ForestWorld>();
  auto &input = world.getResource<input::InputState>();
  auto &actions = world.getResource<input::ActionState>();
  ecs::EntityID camEntity = world.getResource<camera::MainCamera>().entity;
  core::TransformComponent &camTf = world.getComponent<core::TransformComponent>(camEntity);

  world.query().write<core::TransformComponent, PlayerComponent>().each(
      [&](core::TransformComponent &tf, PlayerComponent &p) {
        // --- Look --------------------------------------------------------
        p.yaw += input.getMouseDeltaX() * kMouseSensitivity;
        p.pitch -= input.getMouseDeltaY() * kMouseSensitivity;
        p.pitch = std::clamp(p.pitch, -88.0f, 88.0f);

        float yawRad = core::radians(p.yaw);
        Vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
        Vec3 right(-forward.z, 0.0f, forward.x);

        // --- Swimming ------------------------------------------------------
        if (!p.swimming && depthBelowSurface(tf.position, t) > kSwimDepth) {
          p.swimming = true;
          p.onGround = false;
        }
        if (p.swimming) {
          swimStep(fw, p, tf, camTf, actions, forward, right, dt, t);
          return;
        }

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

        // Coyote time: still allowed to jump for a moment after walking off an edge.
        p.coyote = p.onGround ? kCoyoteTime : std::max(p.coyote - dt, 0.0f);
        if (p.coyote > 0.0f && actions.pressed(input::Action::Jump)) {
          p.velocity.y = kJump;
          p.onGround = false;
          p.coyote = 0.0f;
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
        resolveObstacles(fw, nx, nz, tf.position.y, p.velocity, p.onGround ? 0.12f : kAirStepUp,
                         kPushSpeed * dt);

        float lim = fw.terrain.half() - 12.0f;
        nx = std::clamp(nx, -lim, lim);
        nz = std::clamp(nz, -lim, lim);

        // --- Vertical ----------------------------------------------------------
        float y = tf.position.y + p.velocity.y * dt;
        Ground g = groundAt(fw, nx, nz, tf.position.y, p.onGround ? kStepUp : kAirStepUp);
        const float ground = g.y;
        if (y <= ground) {
          y = ground;
          if (p.velocity.y < 0.0f) p.velocity.y = 0.0f;
          p.onGround = true;
        } else if (p.onGround && p.velocity.y <= 0.0f && y - ground < 0.4f) {
          y = ground;  // walking downhill: stay on the surface instead of hopping
        } else {
          p.onGround = false;
        }
        if (g.slide && y <= ground + 1e-4f) {
          // Landed on top of a rock or trunk: slide off it, away from its
          // centre, instead of being shoved out sideways once the feet sink in.
          float sx = nx - g.slideX, sz = nz - g.slideZ;
          float sd = std::sqrt(sx * sx + sz * sz);
          if (sd > 1e-4f) { sx /= sd; sz /= sd; }
          else { sx = 1.0f; sz = 0.0f; }
          float into = p.velocity.x * sx + p.velocity.z * sz;
          if (into < kSlideSpeed) {
            p.velocity.x += (kSlideSpeed - into) * sx;
            p.velocity.z += (kSlideSpeed - into) * sz;
          }
          nx += sx * kSlideSpeed * dt;
          nz += sz * kSlideSpeed * dt;
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
