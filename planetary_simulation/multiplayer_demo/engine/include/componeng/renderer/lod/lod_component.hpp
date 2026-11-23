#pragma once

#include "componeng/core/types.hpp"

#include <cmath>

namespace componeng::renderer {

/**
 * @brief Level of detail for an entity's mesh, by distance or by how much of
 * the screen the entity covers.
 *
 * LodSystem picks the first level whose mesh exists and whose range still
 * covers the entity's distance to the main camera, writes it to
 * MeshComponent::meshID, and hides the entity (MeshComponent::visible = false)
 * once it is beyond every level. Give the entity a MeshComponent as usual;
 * its meshID is overwritten by the system.
 *
 * `mesh[k]` is a mesh handle from AssetManager::getMeshID, or 0 for "no mesh
 * at this level" (e.g. small props that simply disappear far away, or parts
 * that only exist up close). `distance[k]` is the range of level k in metres;
 * level 0 is the most detailed. Levels must be listed nearest first.
 *
 * Screen-height mode: set `size` (> 0) and the levels are chosen by how tall
 * the entity looks on screen instead of by metres. The entity's height on
 * screen, as a fraction of the screen's, is
 *
 *     size * max(|scale|) / (2 * distance * tan(fov / 2))
 *
 * (`size` is the object's extent in model space, its scale applied on top, and
 * `fov` is the main camera's vertical field of view). Level k is used while
 * that fraction is at least `screenHeight[k]`; give the levels nearest first,
 * so the thresholds fall (0.3, 0.1, 0.02...). A threshold of 0 never gives up,
 * so the last level can stay visible at any distance; a level with a positive
 * threshold hides the entity once it shrinks below it. The result does not
 * depend on the window's resolution, and bigger objects hold their detail
 * further away than small ones. lodScreenHeight() converts a distance you have
 * in mind into a threshold.
 *
 * Entities that make up one object (a tree's trunk and leaves) switch
 * together as long as they share a position and ranges (and, in screen-height
 * mode, a size and scale).
 *
 * Cross-fade: with RenderSettings::lodDither on (the default) a level change
 * dissolves over `lodFadeSeconds` instead of popping, drawing both meshes
 * through complementary dither patterns. The material's shader must declare
 * `instanceFade` and discard on it (diffuse.frag and pbr.frag do); others keep
 * the hard switch. Returning to the level being faded out reverses the fade; a
 * change to a third level restarts it, dropping the old mesh at once.
 */
struct LodComponent {
  static constexpr const char *component_name = "LodComponent";
  static constexpr int kMaxLevels = 3;

  core::HandleID mesh[kMaxLevels] = {0, 0, 0};
  float distance[kMaxLevels] = {0.0f, 0.0f, 0.0f};

  // Screen-height mode, used when `size` > 0 (otherwise `distance` is).
  float size = 0.0f;
  float screenHeight[kMaxLevels] = {0.0f, 0.0f, 0.0f};

  // Runtime: the level chosen last time, so unchanged entities cost one
  // compare.
  int level = -1;

  // Runtime: cross-fade state. `fromLevel` is the level being faded out
  // (kMaxLevels for "nothing"), `transition` how far the fade to `level` has
  // got: 0 just started, 1 settled.
  int fromLevel = kMaxLevels;
  float transition = 1.0f;
};

/**
 * @brief The screen-height threshold at which an object of world-space extent
 * `worldSize` (size times its largest scale) is `distance` metres from a camera
 * with vertical field of view `fovDegrees`. Use it to say "switch at 60 m".
 */
inline float lodScreenHeight(float worldSize, float distance,
                             float fovDegrees) {
  const float halfFov = 0.5f * fovDegrees * 3.14159265358979f / 180.0f;
  return worldSize / (2.0f * distance * std::tan(halfFov));
}

} // namespace componeng::renderer
