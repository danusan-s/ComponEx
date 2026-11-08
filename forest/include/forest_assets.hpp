#pragma once

#include "componeng/ecs/world.hpp"

#include <string>

namespace forest {

struct ForestWorld;

// Loads the shaders, textures and meshes and registers every material. The
// terrain's height map and the grass mask are made from `forest` and handed to
// the grass material.
void loadForestAssets(componeng::ecs::World &world, const ForestWorld &forest);

struct GrassLevel;

// Registers the material for one chunk of grass: `level`'s settings, the chunk's
// corner (world xz) and the offset that keeps its random numbers its own.
// `padding` is how far beyond its mesh the chunk's tufts reach, for culling.
void registerGrassChunkMaterial(componeng::ecs::World &world, const char *name,
                                const GrassLevel &level, int levelIndex, float cornerX,
                                float cornerZ, float seedOffset, float padding);

// Whether trees use their decimated LOD1 and LOD2 meshes between LOD0 and the
// billboard. Off by default: at those decimation ratios the trunks turn into dark
// pyramids and spikes (the leaves into big flat wedges), which is worse than going
// straight from the full mesh to the billboard. FOREST_TREE_MID_LODS turns them on.
bool treeMidLods();

// Name of the mesh for a model part at a level of detail, e.g. "fir_a_leaf_lod1".
// Not every level exists for every part; check with AssetManager before using.
std::string meshName(const char *model, const char *part, int lod);

// Whether that mesh file existed and was loaded.
bool meshLoaded(const std::string &name);

} // namespace forest
