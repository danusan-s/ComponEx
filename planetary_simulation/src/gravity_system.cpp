#include "gravity_system.hpp"
#include "celestial_component.hpp"

#include "componeng/components/rigidbody_component.hpp"
#include "componeng/components/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/world.hpp"

namespace planetary_sim {

using namespace componeng;
using namespace components;

const float G = 1e-3f; // Gravitational constant (scaled for simulation)

void GravitySystem::onUpdate(const ecs::SystemState &state) {
  float dt = state.deltaTime;

  auto view =
      state.world
          ->query<TransformComponent, RigidBodyComponent, CelestialComponent>();

  std::vector<std::tuple<TransformComponent &, RigidBodyComponent &,
                         CelestialComponent &>>
      planets;

  for (auto &&item : view) {
    planets.emplace_back(item);
  }

  for (size_t i = 0; i < planets.size(); ++i) {
    auto &[transformA, rigidBodyA, celestialA] = planets[i];

    for (size_t j = i + 1; j < planets.size(); ++j) {
      auto &[transformB, rigidBodyB, celestialB] = planets[j];

      // Calculate the gravitational force between the two celestial bodies
      core::Vec3 direction = transformB.position - transformA.position;
      float distanceSquared = glm::dot(direction, direction);
      float distance = glm::sqrt(distanceSquared);

      if (distance > 0.0f) {
        core::Vec3 normalizedDirection = direction / distance;
        core::Vec3 accelerationA =
            (G * rigidBodyB.mass) * direction / distanceSquared;
        core::Vec3 accelerationB =
            -(G * rigidBodyA.mass) * direction / distanceSquared;

        // Update the velocities of the celestial bodies
        rigidBodyA.velocity += accelerationA * dt;
        rigidBodyB.velocity += accelerationB * dt;
      }
    }
  }
}

} // namespace planetary_sim
