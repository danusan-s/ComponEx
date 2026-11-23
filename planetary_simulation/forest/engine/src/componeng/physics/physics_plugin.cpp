#include "componeng/physics/physics_plugin.hpp"

#include "componeng/core/engine.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/physics/collider_component.hpp"
#include "componeng/physics/physics_config.hpp"
#include "componeng/physics/physics_system.hpp"
#include "componeng/physics/rigidbody_component.hpp"

namespace componeng::physics {

void PhysicsPlugin::build(core::Engine &engine) {
  ecs::World &world = engine.world();
  world.registerComponents<core::TransformComponent, RigidBodyComponent,
                           ColliderComponent>();
  world.setResource(PhysicsConfig());
  world.registerSystem<PhysicsSystem>(ecs::SystemGroup::Simulation);
}

} // namespace componeng::physics
