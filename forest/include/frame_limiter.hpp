#pragma once

#include "componeng/ecs/system.hpp"

#include <chrono>

namespace forest {

// Caps the frame rate. The engine's loop has no limiter (and vsync is off), so
// this runs last in the frame, after everything has been drawn, and sleeps until
// the next frame is due. It paces against a running deadline rather than "frame
// time since last frame", so one slow frame does not make the following ones
// overshoot, and a long stall does not trigger a burst of catch-up frames.
//
// The target is cfg::kTargetFps; FOREST_FPS=<n> overrides it and 0 turns the
// cap off. FOREST_SHOT runs uncapped unless FOREST_FPS is set, so its "avg fps"
// stays a measure of cost.
class FrameLimiterSystem : public componeng::ecs::ISystem {
public:
  void onCreate(const componeng::ecs::SystemState &state) override;
  void onUpdate(const componeng::ecs::SystemState &state) override;

private:
  using Clock = std::chrono::steady_clock;

  Clock::duration m_period{};  // zero = uncapped
  Clock::time_point m_deadline{};
  bool m_started = false;
};

} // namespace forest
