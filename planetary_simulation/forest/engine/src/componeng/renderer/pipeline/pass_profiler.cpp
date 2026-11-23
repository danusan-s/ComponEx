#include "componeng/renderer/pipeline/pass_profiler.hpp"

#include "componeng/core/profiling.hpp"
#include "componeng/renderer/asset/shader.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/resources/render_stats.hpp"

namespace componeng::renderer {

namespace {

double msSince(std::chrono::steady_clock::time_point t) {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - t)
      .count();
}

} // namespace

PassProfiler::PassProfiler(api::IRenderDevice &device,
                           resources::RenderStats &stats)
    : m_device(device), m_stats(stats) {
}

void PassProfiler::beginPass(api::PassId pass) {
  if constexpr (core::Profiling::kEnabled) {
    m_device.beginGpuPass(pass);
    m_passStart = Clock::now();
  }
}

void PassProfiler::endPass(api::PassId pass) {
  if constexpr (core::Profiling::kEnabled) {
    m_stats.of(pass).cpuMs = msSince(m_passStart);
    m_device.endGpuPass(pass);
  }
}

void PassProfiler::timeShader(const Shader *next) {
  if constexpr (core::Profiling::kEnabled) {
    if (m_timedShader) {
      m_timedShader->endGpuTimer();
    }
    if (next) {
      next->beginGpuTimer();
    }
    m_timedShader = next;
  } else {
    (void)next;
  }
}

void PassProfiler::beginUpload() {
  if constexpr (core::Profiling::kEnabled) {
    m_uploadStart = Clock::now();
  }
}

void PassProfiler::endUpload() {
  if constexpr (core::Profiling::kEnabled) {
    m_stats.uploadCpuMs = msSince(m_uploadStart);
  }
}

} // namespace componeng::renderer
