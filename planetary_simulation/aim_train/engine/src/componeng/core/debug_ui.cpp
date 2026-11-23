#include "componeng/core/debug_ui.hpp"

#include "componeng/ecs/world.hpp"
#include "componeng/utils/logger.hpp"
#include "imgui/imgui.h"

#include <string>
#include <vector>

namespace componeng::core {

// The add* calls only mean something between beginFrame() and endFrame();
// outside that window (headless runs, tests) they are ignored instead of
// reaching ImGui without a frame in progress.
static bool s_frameActive = false;

// Tabs share one tab bar. With the debug panels on it sits in "Debug Info",
// below whatever was added before the first tab. With them off there is no such
// window, so the first tab opens a "Profiling" one to hold the bar.
static bool s_tabBarTried = false; // BeginTabBar was asked this frame
static bool s_tabBarOpen = false;  // and said yes: EndTabBar is owed
static bool s_tabItemOpen = false; // inside a selected tab: EndTabItem is owed
static bool s_profilingWindow = false; // we began "Profiling": End is owed

// The panel window sits in the top-left corner and sizes itself to its
// contents, so no text is ever cut off. It can't be moved or resized by hand.
static constexpr ImGuiWindowFlags kPanelFlags =
    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoSavedSettings;

static void beginPanelWindow(const char *title) {
  ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
  ImGui::Begin(title, nullptr, kPanelFlags);
}

void DebugUI::init(ecs::World &world, void *windowHandle) {
  LOG_INFO("Initializing UI System...");

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.FontGlobalScale = 1.5f;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  world.getRenderDevice().initImGui(windowHandle);
}

void DebugUI::shutdown(ecs::World &world) {
  LOG_INFO("Shutting down UI System...");
  world.getRenderDevice().shutdownImGui();
  ImGui::DestroyContext();
}

void DebugUI::beginFrame(ecs::World &world) {
  world.getRenderDevice().imguiNewFrame();
  ImGui::NewFrame();
  // The default window only exists for the debug panels; without them nothing
  // is added outside a pane of its own.
  if constexpr (kEnabled) {
    beginPanelWindow("Debug Info");
  }
  s_tabBarTried = s_tabBarOpen = s_tabItemOpen = s_profilingWindow = false;
  s_frameActive = true;
}

void DebugUI::endFrame(ecs::World &world) {
  s_frameActive = false;
  if (s_tabItemOpen)
    ImGui::EndTabItem();
  if (s_tabBarOpen)
    ImGui::EndTabBar();
  if (s_profilingWindow)
    ImGui::End();
  s_tabBarTried = s_tabBarOpen = s_tabItemOpen = s_profilingWindow = false;
  if constexpr (kEnabled)
    ImGui::End();
  ImGui::Render();
  world.getRenderDevice().imguiRender();
}

void DebugUI::showFps(float fps, float avgFps) {
  if (!s_frameActive)
    return;
  if constexpr (kEnabled) {
    detail::addValue("FPS", fps);
    detail::addValue("Avg FPS", avgFps);
  } else {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("FPS", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Avg FPS: %.1f", avgFps);
    ImGui::End();
  }
}

namespace detail {

void addText(const std::string &label, const std::string &value) {
  if (!s_frameActive)
    return;
  ImGui::Text("%s: %s", label.c_str(), value.c_str());
}

void addValue(const std::string &label, float v) {
  if (!s_frameActive)
    return;
  ImGui::Text("%s: %.2f", label.c_str(), v);
}

bool addSection(const std::string &label) {
  if (!s_frameActive)
    return false;
  return ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
}

bool beginTab(const std::string &label) {
  if (!s_frameActive)
    return false;
  if (!s_tabBarTried) {
    s_tabBarTried = true;
    if constexpr (!kDebugPanelsEnabled) {
      beginPanelWindow("Profiling");
      s_profilingWindow = true;
    }
    s_tabBarOpen = ImGui::BeginTabBar("##debug_tabs");
  }
  if (!s_tabBarOpen)
    return false;
  s_tabItemOpen = ImGui::BeginTabItem(label.c_str());
  return s_tabItemOpen;
}

void endTab() {
  if (!s_frameActive || !s_tabItemOpen)
    return;
  ImGui::EndTabItem();
  s_tabItemOpen = false;
}

void addVec3(const std::string &label, const Vec3 &v) {
  if (!s_frameActive)
    return;
  ImGui::Text("%s: (%.2f, %.2f, %.2f)", label.c_str(), v.x, v.y, v.z);
}

void addSlider(const std::string &label, float *v, float min, float max) {
  if (!s_frameActive)
    return;
  ImGui::SliderFloat(label.c_str(), v, min, max);
}

bool addCheckbox(const std::string &label, bool *v) {
  if (!s_frameActive)
    return false;
  return ImGui::Checkbox(label.c_str(), v);
}

} // namespace detail

} // namespace componeng::core
