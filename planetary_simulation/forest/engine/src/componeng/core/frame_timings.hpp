#pragma once

#include "componeng/core/debug_ui.hpp"
#include "componeng/core/string_interner.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/backend/api/pass_id.hpp"
#include "componeng/resources/render_stats.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace componeng::core {

/**
 * Averages samples over a window and shows the last completed average.
 *
 * add() accumulates every frame; publish() turns what has accumulated into the
 * displayed value and starts a new window. A number that only changes a couple
 * of times a second is readable at any frame rate, which a per-frame moving
 * average is not (at hundreds of fps it still redraws different digits every
 * frame). Negative samples mean "no data" and are ignored; `value` stays
 * negative until the first window completes.
 */
struct Smoothed {
  double value = -1.0;

  void add(double sample) {
    if (sample < 0.0)
      return;
    m_sum += sample;
    ++m_count;
  }

  void publish() {
    if (m_count > 0)
      value = m_sum / static_cast<double>(m_count);
    m_sum = 0.0;
    m_count = 0;
  }

private:
  double m_sum = 0.0;
  int m_count = 0;
};

/**
 * Per-frame CPU and GPU timings for the debug UI.
 *
 *  - CPU frame: from the start of the frame to just before the buffer swap,
 *    i.e. all systems plus submitting the UI. Includes the time spent issuing
 *    GL calls, but not waiting at the swap.
 *  - Swap + events: the swap and event polling. Large here means the CPU is
 *    waiting on the GPU (or the driver), i.e. the frame is GPU-bound.
 *  - GPU frame: GPU time for the frame's commands, measured with timer
 *    queries (a few frames behind, see IRenderDevice::beginGpuTimer).
 *  - Per system: each system's onUpdate wall time.
 *  - Per render pass: GPU time (timestamp queries, like the frame's), the CPU
 *    time spent submitting it, and what it drew (resources::RenderStats).
 *
 * The numbers are shown in separate panes: "Frame timing (ms)" for the frame
 * and the systems, "Render passes" for the passes, "Shader GPU (ms)" for the
 * shaders.
 *
 * Each frame's samples are accumulated and the displayed numbers refresh every
 * kWindowSeconds with the average over that window (and once right after the
 * first frame, so the panel is not blank at start-up).
 */
class FrameTimings {
public:
  static constexpr double kWindowSeconds = 0.5;

  void record(const ecs::World &world, const renderer::AssetManager *assets,
              const renderer::api::IRenderDevice &device,
              const resources::RenderStats *stats, double deltaSeconds,
              double cpuMs, double swapMs, double gpuMs) {
    m_cpu.add(cpuMs);
    m_swap.add(swapMs);
    m_gpu.add(gpuMs);

    if (stats) {
      for (std::size_t i = 0; i < renderer::api::kPassCount; ++i) {
        const auto pass = static_cast<renderer::api::PassId>(i);
        const resources::PassStats &p = stats->of(pass);
        PassTimes &t = m_passes[i];
        t.gpu.add(device.lastGpuPassTimeMs(pass));
        t.cpu.add(p.cpuMs);
        t.draws.add(p.drawCalls);
        t.instances.add(static_cast<double>(p.instances));
        t.triangles.add(static_cast<double>(p.triangles));
        t.shaderBinds.add(p.shaderBinds);
      }
      m_uploadMs.add(stats->uploadCpuMs);
      m_uploadBytes.add(static_cast<double>(stats->uploadBytes));
    }

    if (assets) {
      assets->forEachShader([&](Name name, const renderer::Shader &shader) {
        auto it = std::find_if(
            m_shaders.begin(), m_shaders.end(),
            [&](const SystemTime &s) { return s.name == name.c_str(); });
        if (it == m_shaders.end()) {
          m_shaders.push_back({name.c_str(), {}});
          it = m_shaders.end() - 1;
        }
        it->ms.add(shader.lastGpuTimeMs());
      });
    }

    std::size_t i = 0;
    world.forEachSystemTiming([&](const char *name, double ms) {
      if (i >= m_systems.size())
        m_systems.push_back({name, {}});
      m_systems[i++].ms.add(ms);
    });

    m_elapsed += deltaSeconds;
    if (!m_published || m_elapsed >= kWindowSeconds) {
      m_cpu.publish();
      m_swap.publish();
      m_gpu.publish();
      for (PassTimes &t : m_passes) {
        t.gpu.publish();
        t.cpu.publish();
        t.draws.publish();
        t.instances.publish();
        t.triangles.publish();
        t.shaderBinds.publish();
      }
      m_uploadMs.publish();
      m_uploadBytes.publish();
      for (SystemTime &s : m_systems)
        s.ms.publish();
      for (SystemTime &s : m_shaders)
        s.ms.publish();
      m_elapsed = 0.0;
      m_published = true;
    }
  }

  void show() const {
    if (ProfilerUI::beginTab("Frame (ms)")) {
      ProfilerUI::addValue("CPU frame", static_cast<float>(m_cpu.value));
      if (m_gpu.value >= 0.0)
        ProfilerUI::addValue("GPU frame", static_cast<float>(m_gpu.value));
      else
        ProfilerUI::addText("GPU frame", "n/a");
      ProfilerUI::addValue("Swap + events", static_cast<float>(m_swap.value));

      if (ProfilerUI::addSection("System CPU (ms), slowest first"))
        showSorted(m_systems);
    }
    ProfilerUI::endTab();

    if (ProfilerUI::beginTab("Render passes"))
      showPasses();
    ProfilerUI::endTab();

    if (ProfilerUI::beginTab("Shaders")) {
      ProfilerUI::addText("Shader GPU (ms)", "slowest first");
      showSorted(m_shaders);
    }
    ProfilerUI::endTab();
  }

private:
  struct SystemTime {
    std::string name;
    Smoothed ms;
  };

  // One render pass's averaged timings and counters.
  struct PassTimes {
    Smoothed gpu;
    Smoothed cpu;
    Smoothed draws;
    Smoothed instances;
    Smoothed triangles;
    Smoothed shaderBinds;
  };

  // 18200 -> "18.2k", 1234567 -> "1.23M"; small numbers as they are.
  static std::string count(double v) {
    char buf[32];
    if (v >= 1e6)
      std::snprintf(buf, sizeof buf, "%.2fM", v / 1e6);
    else if (v >= 1e4)
      std::snprintf(buf, sizeof buf, "%.1fk", v / 1e3);
    else
      std::snprintf(buf, sizeof buf, "%.0f", v);
    return buf;
  }

  static std::string ms(double v) {
    char buf[32];
    if (v < 0.0)
      return "n/a";
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
  }

  // One line per pass, in the order the frame runs them:
  //   GPU ms | CPU submit ms | draw calls | instances | triangles | shader
  //   binds
  // The GPU time is a few frames old, and the passes' GPU times do not have to
  // add up to "GPU frame" (the UI itself is in that one, and so is any idle
  // gap between passes).
  void showPasses() const {
    double gpuSum = 0.0, cpuSum = 0.0, draws = 0.0, instances = 0.0,
           triangles = 0.0, binds = 0.0;
    bool gpuKnown = true, any = false;
    for (std::size_t i = 0; i < renderer::api::kPassCount; ++i) {
      const PassTimes &t = m_passes[i];
      if (t.draws.value < 0.0) // no window has completed yet
        continue;
      any = true;
      if (t.gpu.value >= 0.0)
        gpuSum += t.gpu.value;
      else
        gpuKnown = false;
      cpuSum += t.cpu.value;
      draws += t.draws.value;
      instances += t.instances.value;
      triangles += t.triangles.value;
      binds += t.shaderBinds.value;
      ProfilerUI::addText(
          renderer::api::passName(static_cast<renderer::api::PassId>(i)),
          "GPU " + ms(t.gpu.value) + " | CPU " + ms(t.cpu.value) + " | " +
              count(t.draws.value) + " draws | " + count(t.instances.value) +
              " inst | " + count(t.triangles.value) + " tris | " +
              count(t.shaderBinds.value) + " shaders");
    }
    if (!any)
      return;
    ProfilerUI::addText("All passes",
                        "GPU " + ms(gpuKnown ? gpuSum : -1.0) + " | CPU " +
                            ms(cpuSum) + " | " + count(draws) + " draws | " +
                            count(instances) + " inst | " + count(triangles) +
                            " tris | " + count(binds) + " shaders");
    if (m_uploadMs.value >= 0.0)
      ProfilerUI::addText("Instance upload",
                          "CPU " + ms(m_uploadMs.value) + " | " +
                              count(m_uploadBytes.value / 1024.0) + " KiB");
  }

  static void showSorted(const std::vector<SystemTime> &times) {
    std::vector<const SystemTime *> order;
    order.reserve(times.size());
    for (const SystemTime &s : times) {
      if (s.ms.value >= 0.0) // appeared since the last refresh
        order.push_back(&s);
    }
    std::sort(order.begin(), order.end(),
              [](const SystemTime *a, const SystemTime *b) {
                return a->ms.value > b->ms.value;
              });
    for (const SystemTime *s : order)
      ProfilerUI::addValue(s->name, static_cast<float>(s->ms.value));
  }

  double m_elapsed = 0.0;
  bool m_published = false;

  Smoothed m_cpu;
  Smoothed m_swap;
  Smoothed m_gpu;
  PassTimes m_passes[renderer::api::kPassCount];
  Smoothed m_uploadMs;
  Smoothed m_uploadBytes;
  std::vector<SystemTime> m_systems;
  std::vector<SystemTime> m_shaders;
};

} // namespace componeng::core
