#pragma once

// Forward declaration to avoid circular dependencies
namespace componeng::ecs {
class World;
} // namespace componeng::ecs

namespace componeng::ecs {

/**
 * @brief Per-frame state passed to every system during its lifecycle callbacks.
 *
 * Contains a pointer to the World for entity queries and the elapsed
 * frame time (deltaTime) for time-based updates.
 */
struct SystemState {
  World *world = nullptr;
  float deltaTime = 0.0f;
};

/**
 * @brief Abstract base class for all ECS systems.
 *
 * Systems contain the game logic and operate on entities via queries.
 * Each system is assigned to a SystemGroup which determines its
 * coarse execution order within the frame.
 */
class ISystem {
public:
  virtual ~ISystem() = default;
  virtual void onUpdate(const SystemState &state) = 0;
  virtual void onCreate(const SystemState &) {
  }
  virtual void onDestroy(const SystemState &) {
  }
};

} // namespace componeng::ecs
