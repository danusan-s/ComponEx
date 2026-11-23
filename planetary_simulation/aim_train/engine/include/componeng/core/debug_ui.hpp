#pragma once

#include "componeng/core/profiling.hpp"
#include "componeng/core/types.hpp"

#include <string>

namespace componeng::ecs {
class World;
}

namespace componeng::core {

#ifdef COMPONENG_DISABLE_DEBUG_UI
inline constexpr bool kDebugPanelsEnabled = false;
#else
inline constexpr bool kDebugPanelsEnabled = true;
#endif

namespace detail {
// The ImGui-backed implementations, always built (ImGui is always linked).
// Call them through DebugUI or ProfilerUI, which compile the call away.
void addText(const std::string &label, const std::string &value);
void addValue(const std::string &label, float v);
void addVec3(const std::string &label, const Vec3 &v);
bool addSection(const std::string &label);
bool beginTab(const std::string &label);
void endTab();
void addSlider(const std::string &label, float *v, float min, float max);
bool addCheckbox(const std::string &label, bool *v);
} // namespace detail

/**
 * @brief The calls that put values on screen. `Active` false makes every one
 * an empty inline, so call sites cost nothing (no string is built, no call is
 * made) and nothing is drawn.
 *
 * Everything lands in the default "Debug Info" window, above its tab bar.
 * beginTab()/endTab() wrap a group of add* calls in a tab of that window's tab
 * bar, so the timing views sit side by side instead of stacking in one tall
 * list. (With the debug panels off there is no "Debug Info"; the first tab
 * opens a "Profiling" window to hold the bar.) Add the plain lines before the
 * first tab: anything added while a tab is open belongs to it.
 */
template <bool Active> struct PanelApi {
  static void addText(const std::string &label, const std::string &value) {
    if constexpr (Active)
      detail::addText(label, value);
  }
  static void addValue(const std::string &label, float v) {
    if constexpr (Active)
      detail::addValue(label, v);
  }
  static void addVec3(const std::string &label, const Vec3 &v) {
    if constexpr (Active)
      detail::addVec3(label, v);
  }
  static void addSlider(const std::string &label, float *v, float min,
                        float max) {
    if constexpr (Active)
      detail::addSlider(label, v, min, max);
  }

  /** A checkbox bound to `*v`. Returns true on the frame it was clicked. */
  static bool addCheckbox(const std::string &label, bool *v) {
    if constexpr (Active)
      return detail::addCheckbox(label, v);
    return false;
  }

  /** Starts a collapsible group (open by default); the add* calls that follow
   *  until the next section belong to it. Returns false when it is collapsed,
   *  so the caller can skip building its contents. */
  static bool addSection(const std::string &label) {
    if constexpr (Active)
      return detail::addSection(label);
    return false;
  }

  /** Adds a tab called `label`; the add* calls until endTab() go into it.
   *  Returns false while another tab is selected (or the window is
   *  collapsed), so the caller can skip building its contents. endTab() is
   *  safe to call either way:
   *
   *      if (DebugUI::beginTab("Render passes")) { ...add*... }
   *      DebugUI::endTab();
   *
   *  Only valid between beginFrame() and endFrame(). */
  static bool beginTab(const std::string &label) {
    if constexpr (Active)
      return detail::beginTab(label);
    return false;
  }
  static void endTab() {
    if constexpr (Active)
      detail::endTab();
  }
};

/**
 * @brief Wrapper around ImGui for showing debug information: the frame
 * lifecycle, an FPS readout, and (PanelApi) the add* / pane calls.
 *
 * Two flags decide what a build shows:
 *  - COMPONENG_DEBUG_UI (default ON): the debug panels, i.e. everything a game
 *    or system adds through DebugUI. OFF turns those calls into empty inlines
 *    and leaves only the small FPS window.
 *  - COMPONENG_PROFILING (default OFF, see Profiling): the timing panes, drawn
 *    through ProfilerUI.
 * ImGui itself is always built, for the FPS window.
 *
 * Code that calls ImGui directly (rather than through DebugUI / ProfilerUI)
 * must stay between beginFrame() and endFrame().
 */
class DebugUI : public PanelApi<kDebugPanelsEnabled> {
public:
  /** False in builds configured with -DCOMPONENG_DEBUG_UI=OFF. Code that does
   *  extra work only to feed the debug panels can skip it with `if constexpr`.
   *  The FPS window is shown either way. */
  static constexpr bool kEnabled = kDebugPanelsEnabled;

  // Setup and teardown of ImGui context and the render device's bindings. The
  // world holds the device; the window is the one ImGui draws into.
  static void init(ecs::World &world, void *windowHandle);
  static void shutdown(ecs::World &world);

  // Convenience functions to wrap ImGui frame calls
  static void beginFrame(ecs::World &world);
  static void endFrame(ecs::World &world);

  /** Shows the frame rate: two lines in "Debug Info" when the debug panels are
   *  on, otherwise a small window of its own in the top-left corner. */
  static void showFps(float fps, float avgFps);
};

/**
 * @brief The same calls as DebugUI, for the measurement tabs (frame, pass and
 * shader timings). Live exactly when Profiling::kEnabled, whatever
 * COMPONENG_DEBUG_UI says.
 */
using ProfilerUI = PanelApi<Profiling::kEnabled>;

} // namespace componeng::core
