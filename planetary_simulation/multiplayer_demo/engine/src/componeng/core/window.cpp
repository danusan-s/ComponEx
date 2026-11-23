#include "componeng/core/window.hpp"

#include "componeng/ecs/world.hpp"
#include "componeng/input/input_state.hpp"
#include "componeng/input/raw_input_state.hpp"
#include "componeng/utils/logger.hpp"
#include <GLFW/glfw3.h>

namespace componeng::core {

// While the cursor is unlocked the position callback ignores movement, so the
// stored mouseX/Y go stale; when the cursor locks again its first event reports
// wherever the real cursor was left, which would read as one huge mouse delta.
// Set on every lock (and at start-up), cleared by that first event, which
// becomes the new baseline instead of a movement.
static bool s_rebaseMouse = true;

static bool isMouseLocked(GLFWwindow *window) {
  return glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
}

static ecs::World *worldOf(GLFWwindow *window) {
  return static_cast<ecs::World *>(glfwGetWindowUserPointer(window));
}

// Null when there is no world, or no input plugin to hand events to.
static input::InputState *inputOf(GLFWwindow *window) {
  ecs::World *world = worldOf(window);
  if (!world || !world->hasResource<input::InputState>())
    return nullptr;
  return &world->getResource<input::InputState>();
}

static void framebufferSizeCallback(GLFWwindow *window, int width, int height) {
  ecs::World *world = worldOf(window);
  if (!world)
    return;
  world->getResource<Window>().setViewport(width, height);
}

static void keyCallback(GLFWwindow *window, int key, int /*scancode*/,
                        int action, int /*mode*/) {
  if (!worldOf(window))
    return;

  if (key == GLFW_KEY_0 && action == GLFW_PRESS) {
    if (isMouseLocked(window)) {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      s_rebaseMouse = true;
    }
  }

  if (!isMouseLocked(window))
    return;

  input::InputState *input = inputOf(window);
  if (!input)
    return;
  input::RawInputState &inputState = input->current_state;

  if (key >= 0 && key < 1024) {
    if (action == GLFW_PRESS) {
      inputState.keys[key] = true;
    } else if (action == GLFW_RELEASE) {
      inputState.keys[key] = false;
    }
  }

  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);
}

static void mouseButtonCallback(GLFWwindow *window, int button, int action,
                                int /*mods*/) {
  input::InputState *input = inputOf(window);
  if (!input)
    return;
  input::RawInputState &inputState = input->current_state;

  if (!isMouseLocked(window))
    return;

  if (button >= 0 && button < 8) {
    inputState.mouseButtons[button] = (action == GLFW_PRESS);
  }
}

static void cursorPosCallback(GLFWwindow *window, double xposIn,
                              double yposIn) {
  if (!isMouseLocked(window))
    return;

  input::InputState *inputPtr = inputOf(window);
  if (!inputPtr)
    return;
  input::InputState &input = *inputPtr;
  input::RawInputState &inputState = input.current_state;

  float xpos = static_cast<float>(xposIn);
  float ypos = static_cast<float>(yposIn);

  if (s_rebaseMouse) {
    // Start the delta from here: the frame's previous position is the stale
    // one from before the unlock, so move it too.
    input.previous_state.mouseX = xpos;
    input.previous_state.mouseY = ypos;
    s_rebaseMouse = false;
  }

  inputState.mouseX = xpos;
  inputState.mouseY = ypos;
}

void Window::init(ecs::World &world, int width, int height, const char *title) {
  this->m_width = width;
  this->m_height = height;

  if (glfwInit() != GLFW_TRUE) {
    LOG_ERROR("Failed to initialize GLFW");
    return;
  }

#ifdef COMPONENG_VULKAN
  // Vulkan owns the surface: GLFW must not create a GL context.
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
#endif

  GLFWmonitor *primaryMonitor = glfwGetPrimaryMonitor();
  if (!primaryMonitor) {
    LOG_ERROR("Failed to get primary monitor");
    glfwTerminate();
    return;
  }
  const GLFWvidmode *videoMode = glfwGetVideoMode(primaryMonitor);
  if (!videoMode) {
    LOG_ERROR("Failed to get video mode for primary monitor");
    glfwTerminate();
    return;
  }

  m_handle = glfwCreateWindow(videoMode->width, videoMode->height, title,
                              primaryMonitor, nullptr);
  if (!m_handle) {
    LOG_ERROR("Failed to create GLFW window");
    glfwTerminate();
    return;
  }

#ifndef COMPONENG_VULKAN
  glfwMakeContextCurrent(m_handle);

  glfwSwapInterval(0);
#endif

  m_renderDevice = &world.getRenderDevice();
  m_renderDevice->init(m_handle);

  glfwSetWindowUserPointer(m_handle, &world);
  glfwSetKeyCallback(m_handle, keyCallback);
  glfwSetMouseButtonCallback(m_handle, mouseButtonCallback);
  glfwSetCursorPosCallback(m_handle, cursorPosCallback);
  glfwSetFramebufferSizeCallback(m_handle, framebufferSizeCallback);
  glfwSetInputMode(m_handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  int framebufferWidth, framebufferHeight;
  glfwGetFramebufferSize(m_handle, &framebufferWidth, &framebufferHeight);
  setViewport(framebufferWidth, framebufferHeight);
}

void Window::setViewport(int w, int h) {
  float targetAspectRatio = 16.0f / 9.0f;
  float currentAspectRatio = static_cast<float>(w) / h;

  if (currentAspectRatio > targetAspectRatio) {
    int newWidth = static_cast<int>(h * targetAspectRatio);
    int xOffset = (w - newWidth) / 2;
    m_renderDevice->setScissor(xOffset, 0, newWidth, h);
    m_renderDevice->setViewport(xOffset, 0, newWidth, h);
    return;
  }

  int newHeight = static_cast<int>(w / targetAspectRatio);
  int yOffset = (h - newHeight) / 2;
  m_renderDevice->setScissor(0, yOffset, w, newHeight);
  m_renderDevice->setViewport(0, yOffset, w, newHeight);
}

void Window::shutdown() {
  // Swapchain and surface have to go before the window they belong to.
  if (m_renderDevice)
    m_renderDevice->shutdown();
  if (m_handle) {
    glfwDestroyWindow(m_handle);
    m_handle = nullptr;
  }
  glfwTerminate();
}

bool Window::shouldClose() const {
  return m_handle ? glfwWindowShouldClose(m_handle) : true;
}

void Window::swapBuffers() {
  if (m_handle && m_renderDevice)
    m_renderDevice->present(m_handle);
}

void Window::pollEvents() {
  glfwPollEvents();
}

} // namespace componeng::core
