#pragma once

#include "componeng/ecs/system.hpp"

#include <string>

namespace forest {

// Debug tool: with FOREST_SHOT=<file>, saves the rendered view as a binary PPM
// after a few frames, prints the average frame rate and closes the window. It reads the demo's own framebuffer,
// so it captures the game and nothing else on the desktop. Turn it into a PNG
// with ImageMagick. FOREST_SHOT_FRAME sets how many frames to wait (default 90).
class ScreenshotSystem : public componeng::ecs::ISystem {
public:
  void onCreate(const componeng::ecs::SystemState &state) override;
  void onUpdate(const componeng::ecs::SystemState &state) override;

private:
  std::string m_path;
  int m_frame = 90;
  int m_count = 0;
  int m_timed = 0;
  float m_time = 0.0f;
};

} // namespace forest
