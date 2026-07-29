#include "gravity_system.hpp"
#include "planet_component.hpp"

#include "componeng/components/transform_component.hpp"
#include "componeng/ecs/world.hpp"

#include <cmath>

namespace planetary_sim {

using namespace componeng;

void GravitySystem::onUpdate(const ecs::SystemState &state) {
    float dt = state.deltaTime;

    state.world->query<components::TransformComponent, PlanetComponent>().each(
        [dt](components::TransformComponent &t, PlanetComponent &p) {
            p.phase += p.orbitSpeed * dt;
            t.position.x = std::cos(p.phase) * p.orbitRadius;
            t.position.z = std::sin(p.phase) * p.orbitRadius;

            t.rotation.y += p.rotationSpeed * dt * 60.0f;
        });
}

}

