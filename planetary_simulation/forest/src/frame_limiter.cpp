#include "frame_limiter.hpp"

#include "forest_config.hpp"

#include <cstdio>
#include <cstdlib>
#include <thread>

namespace forest {

void FrameLimiterSystem::onCreate(const componeng::ecs::SystemState &) {
  double fps = cfg::kTargetFps;
  if (const char *env = std::getenv("FOREST_FPS")) {
    fps = std::atof(env);
  } else if (std::getenv("FOREST_SHOT")) {
    fps = 0.0;
  }
  if (fps > 0.0) {
    m_period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / fps));
    std::printf("Frame limiter: %.0f fps\n", fps);
  }
}

void FrameLimiterSystem::onUpdate(const componeng::ecs::SystemState &) {
  if (m_period == Clock::duration::zero()) return;

  const auto now = Clock::now();
  if (!m_started) {
    m_started = true;
    m_deadline = now;
  }
  m_deadline += m_period;

  // Already a whole frame late (a hitch, or the first frames while loading):
  // start pacing again from now rather than running flat out to catch up.
  if (now > m_deadline + m_period) {
    m_deadline = now;
    return;
  }

  // Sleep most of the wait, then spin the last stretch: sleeps overshoot by a
  // scheduler tick or so, which at 60 fps would be visible as judder.
  constexpr auto kSpin = std::chrono::microseconds(500);
  if (m_deadline - kSpin > Clock::now()) std::this_thread::sleep_until(m_deadline - kSpin);
  while (Clock::now() < m_deadline) std::this_thread::yield();
}

} // namespace forest
