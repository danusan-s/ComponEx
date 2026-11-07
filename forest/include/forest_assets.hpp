#pragma once

#include "componeng/ecs/world.hpp"

#include <string>

namespace forest {

struct ForestWorld;

// Loads the shaders, textures and meshes and registers every material. The
// terrain's height map and the grass mask are made from `forest` and handed to
// the grass material.
void loadForestAssets(componeng::ecs::World &world, const ForestWorld &forest);

// Name of the mesh for a model part at a level of detail, e.g. "fir_a_leaf_lod1".
// Not every level exists for every part; check with AssetManager before using.
std::string meshName(const char *model, const char *part, int lod);

// Whether that mesh file existed and was loaded.
bool meshLoaded(const std::string &name);

} // namespace forest
