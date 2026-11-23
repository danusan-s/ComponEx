#include "componeng/core/window_plugin.hpp"

#include "componeng/core/debug_ui.hpp"
#include "componeng/core/engine.hpp"
#include "componeng/core/profiling.hpp"
#include "componeng/core/window.hpp"
#include "componeng/renderer/asset_manager.hpp"
#ifdef COMPONENG_VULKAN
#include "componeng/renderer/backend/vulkan/vk_render_device.hpp"
#else
#include "componeng/renderer/backend/opengl/gl_render_device.hpp"
#endif
#include "componeng/resources/render_settings.hpp"
#include "componeng/resources/render_stats.hpp"
#include "componeng/utils/logger.hpp"
#include "frame_timings.hpp"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>

namespace componeng::core {
namespace {

// The "Settings" tab: switches for the renderer's runtime settings. They are
// plain fields the renderer reads every frame, so a click takes effect on the
// next one.
void showSettingsTab(ecs::World &world) {
  if (!world.hasResource<resources::RenderSettings>()) {
    return;
  }
  if (DebugUI::beginTab("Settings")) {
    auto &settings = world.getResource<resources::RenderSettings>();
    bool fxaa = settings.antiAliasing == resources::AntiAliasing::Fxaa;
    if (DebugUI::addCheckbox("FXAA", &fxaa)) {
      settings.antiAliasing =
          fxaa ? resources::AntiAliasing::Fxaa : resources::AntiAliasing::None;
    }
    DebugUI::addCheckbox("Depth pre-pass", &settings.depthPrepass);
    // Quantised so dragging remakes the scene target a few times, not on
    // every frame.
    float scale = settings.renderScale;
    DebugUI::addSlider("Render scale", &scale, 0.25f, 2.0f);
    settings.renderScale = std::round(scale * 20.0f) / 20.0f;
    DebugUI::addCheckbox("LOD dithering", &settings.lodDither);
    DebugUI::addSlider("LOD fade (s)", &settings.lodFadeSeconds, 0.0f, 2.0f);
  }
  DebugUI::endTab();
}

void windowedLoop(Engine &engine) {
  ecs::World &world = engine.world();
  Window &window = world.getResource<Window>();
  EngineControl &control = world.getResource<EngineControl>();

  double deltaTime = 0.0;
  double accumulator = 0.0;
  double lastFrame = 0.0;
  long long frameCount = 0;
  double avgFPS = 0.0;

  using Clock = std::chrono::steady_clock;
  const auto msBetween = [](Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  FrameTimings timings;

  while (!window.shouldClose() && !control.exitRequested) {
    double currentFrame = glfwGetTime();
    deltaTime = currentFrame - lastFrame;
    accumulator += deltaTime;
    lastFrame = currentFrame;
    frameCount++;

    if (accumulator >= 1.0) {
      avgFPS = frameCount / accumulator;
      frameCount = 0;
      accumulator = 0.0;
    }

    auto &device = world.getRenderDevice();
    renderer::AssetManager *assets =
        world.hasResource<renderer::AssetManager>()
            ? &world.getResource<renderer::AssetManager>()
            : nullptr;
    const auto cpuStart = Clock::now();

    DebugUI::beginFrame(world);
    if constexpr (Profiling::kEnabled)
      device.beginGpuTimer();
    engine.update(deltaTime);
    DebugUI::showFps(static_cast<float>(1.0 / deltaTime),
                     static_cast<float>(avgFPS));
    if constexpr (Profiling::kEnabled)
      timings.show();
    showSettingsTab(world);
    DebugUI::endFrame(world);
    if constexpr (Profiling::kEnabled)
      device.endGpuTimer();
    if constexpr (Profiling::kEnabled) {
      if (assets)
        assets->collectShaderTimings();
    }

    const auto cpuEnd = Clock::now();
    window.swapBuffers();
    window.pollEvents();

    device.checkErrors();
    if constexpr (Profiling::kEnabled) {
      const resources::RenderStats *renderStats =
          world.hasResource<resources::RenderStats>()
              ? &world.getResource<resources::RenderStats>()
              : nullptr;
      timings.record(world, assets, device, renderStats, deltaTime,
                     msBetween(cpuStart, cpuEnd),
                     msBetween(cpuEnd, Clock::now()), device.lastGpuTimeMs());
    }
  }
}

} // namespace

void WindowPlugin::build(Engine &engine) {
  ecs::World &world = engine.world();
  std::unique_ptr<renderer::api::IRenderDevice> renderDevice =
#ifdef COMPONENG_VULKAN
      std::make_unique<renderer::vulkan::VulkanRenderDevice>();
#else
      std::make_unique<renderer::opengl::GLRenderDevice>();
#endif
  world.setResource<std::unique_ptr<renderer::api::IRenderDevice>>(
      std::move(renderDevice));
  world.setResource<Window>(Window());
  engine.setRunner(windowedLoop);
}

void WindowPlugin::ready(ecs::World &world) {
  Window &window = world.getResource<Window>();
  window.init(world, width, height, title.c_str());
  LOG_INFO("Initialized render device");
  DebugUI::init(world, window.getHandle());
}

void WindowPlugin::cleanup(ecs::World &world) {
  DebugUI::shutdown(world);
  world.getResource<Window>().shutdown();
}

} // namespace componeng::core
