#pragma once

#include "componeng/ecs/system.hpp"

#include <memory>
#include <vector>

namespace componeng::renderer {
class Mesh;
}
namespace componeng::ecs {
class World;
}

namespace forest {

class Terrain;

// A tuft of tapered, leaning blades as one mesh, instanced for every tuft: `blades`
// blades within `radius` metres of the centre, `widthMul` times as wide as usual.
std::unique_ptr<componeng::renderer::Mesh> buildGrassTuft(int blades = 12, float radius = 0.22f,
                                                          float widthMul = 1.0f);

// The original grass: a pool of tufts is one entity (ProceduralInstancesComponent)
// drawing side * side tufts. grass.vert places each from its instance id: a jittered grid of `period`
// metres wrapped around the camera, so the player sees grass everywhere while the
// GPU only draws the pool. These must match what the shader is told through
// material uniforms (see loadForestAssets).
struct GrassPool {
  const char *name;  // of its mesh ("<name>_tuft") and material
  int blades;
  float radius, widthMul;  // the tuft mesh
  float period;
  int side;
  float fadeInStart, fadeInEnd;  // tufts grow from nothing between these distances
                                 // (equal: they do not; nearer ones are not drawn)
  float fadeStart, fadeEnd;      // and shrink to nothing between these
  float bladeLod;                // 1: blades drop out with distance
  int count() const { return side * side; }
};

// FOREST_GRASS_LOD picks how the grass is drawn: 3 (the default) as chunks, below;
// 0 as one pool of full tufts wrapped around the camera, the original, for comparison.
int grassLodMode();
std::vector<GrassPool> grassPools();

// Chunked grass (FOREST_GRASS_LOD=3, the default). The world is cut into square chunks, each its own entity
// (so the engine's frustum culling drops the ones out of view, and GrassChunkSystem
// the ones out of range) drawing kGrassChunkSide^2 tufts. There are rings of them:
// small chunks of dense, full tufts near, then bigger chunks of sparser, coarser
// tufts further out, so the range is long and the vertex work follows the view
// instead of a whole pool being run and mostly thrown away.
inline constexpr int kGrassChunkSide = 48;  // tufts per side of a chunk

struct GrassLevel {
  const char *name;
  float chunkSize;  // metres
  int blades;
  float radius, widthMul;
  float fadeInStart, fadeInEnd;  // see GrassPool
  float fadeStart, fadeEnd;
  float bladeLod;
};
std::vector<GrassLevel> grassLevels();  // empty unless FOREST_GRASS_LOD=3

// On every chunk: how far from the camera it may be seen, from the nearest point
// of its square (the transform holds its centre).
struct GrassChunkComponent {
  static constexpr const char *component_name = "GrassChunkComponent";
  float half;  // half the chunk's side
  float fadeInStart, fadeEnd;
};

// Shows the chunks that are within range of the camera. Runs before the engine's
// CullingSystem.
class GrassChunkSystem : public componeng::ecs::ISystem {
public:
  void onUpdate(const componeng::ecs::SystemState &state) override;
};

// Creates a chunk entity (and its material) for every part of the world with grass.
void spawnGrassChunks(componeng::ecs::World &world, const Terrain &terrain);

} // namespace forest
