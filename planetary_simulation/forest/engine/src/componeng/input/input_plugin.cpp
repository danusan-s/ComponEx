#include "componeng/input/input_plugin.hpp"

#include "componeng/core/engine.hpp"
#include "componeng/input/action_state.hpp"
#include "componeng/input/input_state.hpp"
#include "componeng/input/input_system.hpp"

namespace componeng::input {

void InputPlugin::build(core::Engine &engine) {
  ecs::World &world = engine.world();
  world.setResource(InputState());
  world.setResource(ActionState());
  world.registerSystem<InputSystem>(ecs::SystemGroup::PreUpdate);
  world.registerSystem<InputBufferSystem>(ecs::SystemGroup::FrameEnd);
}

} // namespace componeng::input
