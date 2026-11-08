#pragma once

#include "componeng/utils/utils.hpp"

#include <cstdlib>
#include <string>

namespace forest::cfg {

// World: a square heightfield centred on the origin.
inline constexpr float kWorldSize = 640.0f;
inline constexpr int kCells = 512;      // quads per edge (1.25 m)
inline constexpr int kChunkCells = 64;  // quads per terrain mesh, for culling
inline constexpr unsigned kSeed = 11;

// Resolution of the grass mask (density + baked light), over the whole world:
// finer than the terrain grid so rocks and trunks cut clean holes in the grass.
inline constexpr int kMaskCells = 1024;  // 0.625 m

// The sun. Points towards the light. Low enough for long shadows.
inline constexpr float kSunX = 0.70f, kSunY = 0.52f, kSunZ = 0.42f;

} // namespace forest::cfg

namespace forest {

// Debug switch FOREST_ONLY_GRASS: a scene of terrain and grass alone, to see what
// the grass costs. No trees or props are placed (so no collision, shadows or
// bare patches in the grass either) or loaded.
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
