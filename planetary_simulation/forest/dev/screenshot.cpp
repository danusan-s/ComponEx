#include "screenshot.hpp"

#include "grass.hpp"

#include "componeng/ecs/world.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace forest {

void ScreenshotSystem::onCreate(const componeng::ecs::SystemState &) {
  if (const char *path = std::getenv("FOREST_SHOT")) m_path = path;
  if (const char *frame = std::getenv("FOREST_SHOT_FRAME")) m_frame = std::atoi(frame);
}

void ScreenshotSystem::onUpdate(const componeng::ecs::SystemState &state) {
  using namespace componeng;
  if (m_path.empty()) return;

  if (m_count < m_frame) {
    // Average frame rate over the second half of the wait, once loading has settled.
    if (++m_count > m_frame / 2) {
      m_time += state.deltaTime;
      ++m_timed;
    }
    if (m_count < m_frame) return;
    std::printf("SHOT avg fps %.1f over %d frames\n", float(m_timed) / m_time, m_timed);

    // How many grass chunks went to the GPU this frame, of each ring (by chunk size).
    int drawn[3] = {0, 0, 0}, total = 0;
    state.world->query().read<renderer::MeshComponent, GrassChunkComponent>().each(
        [&](const renderer::MeshComponent &mesh, const GrassChunkComponent &chunk) {
          ++total;
          if (!mesh.visible || mesh.culled) return;
          ++drawn[chunk.half < 12.0f ? 0 : chunk.half < 24.0f ? 1 : 2];
        });
    if (total > 0)
      std::printf("SHOT grass chunks drawn %d + %d + %d of %d\n", drawn[0], drawn[1],
                  drawn[2], total);

    // Ask the render device for the frame. OpenGL has it at once; Vulkan hands it
    // over once the frame is presented, so keep asking on the following frames.
    state.world->getRenderDevice().requestCapture();
  }

  renderer::api::FrameCapture shot;
  if (!state.world->getRenderDevice().takeCapture(shot)) return;

  // The engine letterboxes the view to 16:9; the capture is just that area.
  if (FILE *f = std::fopen(m_path.c_str(), "wb")) {
    std::fprintf(f, "P6\n%d %d\n255\n", shot.width, shot.height);
    std::fwrite(shot.rgb.data(), 1, shot.rgb.size(), f);
    std::fclose(f);
  }
  glfwSetWindowShouldClose(static_cast<GLFWwindow *>(state.world->getWindowHandle()), GLFW_TRUE);
}

} // namespace forest
