#pragma once

#include "componeng/utils/utils.hpp"

#include <cstdlib>
#include <string>

namespace forest::cfg {

// World: a square heightfield centred on the origin.
inline constexpr float kWorldSize = 640.0f;
inline constexpr int kCells = 512;     // quads per edge (1.25 m)
inline constexpr int kChunkCells = 64; // quads per terrain mesh, for culling
inline constexpr unsigned kSeed = 11;

// The sea. The terrain falls away to it at the world's edge (an island), and
// one flat plane, much larger than the world so it reaches the fogged horizon,
// sits at kWaterLevel. Nothing is planted, and no grass grows, near the
// waterline.
inline constexpr float kWaterLevel = -4.0f;
inline constexpr float kWaterSize = 1800.0f; // edge length of the plane
inline constexpr int kWaterCells =
    180; // quads per edge (10 m), for vertex waves later
inline constexpr float kWadeDepth =
    3.0f; // the player can walk this far below the surface

// The camera's vertical field of view in degrees. The screen-height LODs are
// derived from it, so it lives here.
inline constexpr float kFov = 70.0f;

// Frame rate cap (see FrameLimiterSystem). 0 = uncapped.
inline constexpr float kTargetFps = 0.0f;

// Resolution of the grass mask (density + baked light), over the whole world:
// finer than the terrain grid so rocks and trunks cut clean holes in the grass.
inline constexpr int kMaskCells = 1024; // 0.625 m

// The sun. Points towards the light. Low enough for long shadows.
inline constexpr float kSunX = 0.70f, kSunY = 0.52f, kSunZ = 0.42f;

} // namespace forest::cfg

namespace forest {

// Debug switch FOREST_ONLY_GRASS: a scene of terrain and grass alone, to see
// what the grass costs. No trees or props are placed (so no collision, shadows
// or bare patches in the grass either) or loaded.
inline bool onlyGrass() {
  static const bool value = std::getenv("FOREST_ONLY_GRASS") != nullptr;
  return value;
}

// Absolute path of a file under forest/assets, through the engine's resolver
// (main() registers this demo's root with it).
inline std::string assetPath(const std::string &relative) {
  return componeng::utils::Utils::getAssetPath("assets/" + relative);
}

} // namespace forest
