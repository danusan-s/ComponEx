#pragma once

#include "componeng/core/plugin.hpp"

#include <string>

namespace componeng::core {

// The platform: a GLFW window, the render device it draws with, the debug UI,
// and the frame loop that polls the window and swaps buffers (it becomes the
// engine's runner). Raw key and mouse events go to the InputState resource when
// there is one.
//
// Everything else that draws (RenderPlugin) requires this.
class WindowPlugin final : public IPlugin {
public:
  std::string title = "ECS Game";
  int width = 1280;
  int height = 720;

  void build(Engine &engine) override;
  void ready(ecs::World &world) override;
  void cleanup(ecs::World &world) override;
};

} // namespace componeng::core
