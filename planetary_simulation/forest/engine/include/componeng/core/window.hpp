#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include <GLFW/glfw3.h>

namespace componeng::ecs {
class World;
}

namespace componeng::core {

/**
 * @brief Abstraction over GLFW window management and OpenGL context setup.
 *
 * Handles window creation, OpenGL initialization, viewport configuration,
 * and exposes input state. Also provides convenience methods for the
 * main loop in engine (shouldClose, swapBuffers, pollEvents).
 */
class Window {
public:
  // Initialize GLFW, create window and OpenGL context, set up input callbacks.
  // The world must hold the render device, and stay put while the window lives
  // (the callbacks point at it); input goes to its InputState if it has one.
  void init(ecs::World &world, int width, int height, const char *title);

  // Clean up GLFW resources and destroy window
  void shutdown();

  // Convenience methods that wrap GLFW calls for the main loop
  bool shouldClose() const;
  void swapBuffers();
  void pollEvents();

  GLFWwindow *getHandle() const {
    return m_handle;
  }

  // Letterbox the render area to 16:9 inside a w x h framebuffer.
  void setViewport(int w, int h);

private:
  GLFWwindow *m_handle = nullptr;
  componeng::renderer::api::IRenderDevice *m_renderDevice = nullptr;
  int m_width = 0;
  int m_height = 0;
};

} // namespace componeng::core
