#include "forest_assets.hpp"

#include "billboard.hpp"
#include "billboard_data.hpp"
#include "forest_config.hpp"
#include "forest_world.hpp"
#include "grass.hpp"
#include "lens_flare.hpp"
#include "species_data.hpp"
#include "water.hpp"

#include "componeng/renderer/asset/diffuse.hpp"
#include "componeng/renderer/asset/pbr.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/utils/logger.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>
#include <unordered_set>

namespace forest {

using namespace componeng;
using core::Vec3;
using renderer::AssetManager;

namespace {

// Hemisphere ambient: blue from the sky above, warm bounce from the ground.
// The engine's own ambient is a hardcoded constant, so the forest shaders take
// theirs from the material.
constexpr Vec3 kSkyAmbient(0.34f, 0.46f, 0.66f);
constexpr Vec3 kGroundAmbient(0.17f, 0.14f, 0.09f);

void setEnvironment(renderer::IMaterial &m) {
  m.setUniform("skyAmbient", kSkyAmbient);
  m.setUniform("groundAmbient", kGroundAmbient);
}

renderer::api::RenderState opaque(bool doubleSided) {
  renderer::api::RenderState s;
  s.blend = renderer::api::BlendMode::None;
  if (doubleSided) s.cull = renderer::api::CullMode::None;
  return s;
}

void loadTexture(AssetManager &am, const std::string &file, const std::string &name,
                 bool alpha, bool srgb, bool repeat, float anisotropy = 8.0f) {
  renderer::api::TextureDesc desc;
  desc.srgb = srgb;
  desc.anisotropy = anisotropy;
  if (repeat) desc.wrap = renderer::api::TextureWrap::Repeat;
  am.loadTexture(assetPath("textures/" + file).c_str(), alpha, name.c_str(), desc);
}

// Texture sets, named as convert_textures.py wrote them.
void loadWoodSet(AssetManager &am, const std::string &n) {
  loadTexture(am, n + "_albedo.jpg", n + "_albedo", false, true, false);
  loadTexture(am, n + "_normal.jpg", n + "_normal", false, false, false);
  loadTexture(am, n + "_arm.jpg", n + "_arm", false, false, false);
}

void loadFoliage(AssetManager &am, const std::string &n) {
  loadTexture(am, n + "_albedo.png", n + "_albedo", true, true, false);
}

// Which texture sets a model uses.
struct ModelTextures {
  const char *trunk, *branch, *leaf;
};

ModelTextures treeTextures(const char *species) {
  if (std::strncmp(species, "island", 6) == 0) return {"island_trunk", "island_branches", "island_leaves"};
  if (std::strncmp(species, "small", 5) == 0) return {"small_trunk", "small_branches", "small_leaves"};
  return {"fir_branches", nullptr, "fir_twigs"};
}

const char *propTexture(const char *name) {
  if (!std::strcmp(name, "log")) return "deadlog";
  if (!std::strcmp(name, "stump")) return "stump";
  if (!std::strcmp(name, "boulder")) return "boulder";
  return "rockmoss";  // the rock set
}

std::unordered_set<std::string> &loadedMeshes() {
  static std::unordered_set<std::string> names;
  return names;
}

// Not every part has every level of detail (branches only exist up close), so
// load whichever files the converter wrote.
void loadMeshes(AssetManager &am, const char *model, const char *part, int maxLod = 2) {
  for (int lod = 0; lod <= maxLod; ++lod) {
    std::string name = meshName(model, part, lod);
    std::string file = assetPath("models/" + name + ".obj");
    if (std::filesystem::exists(file)) {
      am.loadMesh(file.c_str(), name.c_str());
      loadedMeshes().insert(name);
    }
  }
}

void registerWood(AssetManager &am, const std::string &name, const char *tex, float windAmp,
                  float canopyShade) {
  am.registerPbrMaterial<renderer::PbrMaterial>(
      name.c_str(), "forest_wood", (std::string(tex) + "_albedo").c_str(),
      (std::string(tex) + "_normal").c_str(), (std::string(tex) + "_arm").c_str());
  auto &m = am.getMaterial(am.getMaterialID(name.c_str()));
  m.setRenderState(opaque(true));
  m.setUniform("windAmp", windAmp);
  m.setUniform("canopyShade", canopyShade);
  m.setBoundsPadding(windAmp > 0.0f ? 1.5f : 0.0f);
  setEnvironment(m);
}

void registerFoliage(AssetManager &am, const std::string &name, const char *tex, float windAmp,
                     Vec3 crownCenter, float crownRadius, float crownBottom, float crownTop,
                     float crownBlend) {
  am.registerMaterial<renderer::DiffuseMaterial>(name.c_str(), "forest_foliage",
                                                 (std::string(tex) + "_albedo").c_str());
  auto &m = am.getMaterial(am.getMaterialID(name.c_str()));
  m.setRenderState(opaque(true));
  m.setUniform("windAmp", windAmp);
  m.setUniform("crownCenter", crownCenter);
  m.setUniform("crownRadius", crownRadius);
  m.setUniform("crownBottom", crownBottom);
  m.setUniform("crownTop", crownTop);
  m.setUniform("crownBlend", crownBlend);
  m.setBoundsPadding(1.5f);
  setEnvironment(m);
}

// The distant stand-in for a tree: one quad showing the atlas baked by
// tools/bake_billboards.py. It is shaded like the tree's leaves, so it takes
// the species' crown measurements.
void registerBillboard(AssetManager &am, const TreeSpecies &s) {
  const BillboardSpecies *bb = nullptr;
  for (const BillboardSpecies &b : kBillboards)
    if (!std::strcmp(b.name, s.name)) bb = &b;
  if (!bb) return;

  std::string name = std::string(s.name) + "_billboard";
  // Mipmapped, but the views' own borders are transparent so they never bleed.
  loadTexture(am, name + ".png", name + "_albedo", true, true, false, 4.0f);
  am.addMesh(name.c_str(), buildBillboardQuad(bb->halfWidth, bb->bottom, bb->top));
  am.registerMaterial<renderer::DiffuseMaterial>(name.c_str(), "forest_billboard",
                                                 (name + "_albedo").c_str());
  auto &m = am.getMaterial(am.getMaterialID(name.c_str()));
  m.setRenderState(opaque(true));
  m.setUniform("windAmp", 1.0f);
  m.setUniform("views", float(kBillboardViews));
  m.setUniform("atlasCols", float(kBillboardCols));
  m.setUniform("atlasRows", float(kBillboardRows));
  m.setUniform("crownCenterY", s.crownY);
  m.setUniform("crownRadius", s.crownRadius * 1.1f);
  m.setUniform("crownHalfH", std::max(0.5f * (s.crownTop - s.crownBottom), 0.5f));
  m.setUniform("crownBottom", s.crownBottom);
  m.setUniform("crownTop", s.crownTop);
  // The quad turns in the shader, so its mesh bounds say little about where it
  // is drawn; the widest tree scale keeps it from being culled early.
  m.setBoundsPadding(bb->halfWidth * s.maxScale + 1.5f);
  setEnvironment(m);
}

// Everything a grass material tells grass.vert: the ground it grows on, how it is
// laid out (a wrapped pool of `period` metres, or, if chunkSize > 0, one fixed chunk,
// see chunkInfo in the shader) and how it fades.
void configureGrass(AssetManager &am, renderer::IMaterial &m, int side, float period,
                    float chunkSize, Vec3 chunkInfo, float fadeInStart, float fadeInEnd,
                    float fadeStart, float fadeEnd, float bladeLod, int debugIndex) {
  m.setRenderState(opaque(true));
  m.setTexture("heightMap", am.getTextureID("terrain_heights"));
  m.setTexture("grassMask", am.getTextureID("grass_mask"));
  m.setUniform("worldHalf", 0.5f * cfg::kWorldSize);
  m.setUniform("heightCells", float(cfg::kCells));
  m.setUniform("tuftSide", float(side));
  m.setUniform("period", period);
  m.setUniform("chunkSize", chunkSize);
  m.setUniform("chunkInfo", chunkInfo);
  m.setUniform("fadeInStart", fadeInStart);
  m.setUniform("fadeInEnd", fadeInEnd);
  m.setUniform("fadeStart", fadeStart);
  m.setUniform("fadeEnd", fadeEnd);
  m.setUniform("bladeLod", bladeLod);
  // FOREST_GRASS_DEBUG=1 tints each pool or ring of chunks a different colour (blue,
  // red, purple); =2 colours the grass by distance band (see grass.vert).
  static const char *debugEnv = std::getenv("FOREST_GRASS_DEBUG");
  const Vec3 colours[] = {Vec3(0.35f, 0.6f, 2.2f), Vec3(2.4f, 0.4f, 0.4f), Vec3(1.6f, 0.5f, 2.2f)};
  m.setUniform("debugBands", debugEnv && debugEnv[0] == '2' ? 1.0f : 0.0f);
  m.setUniform("debugTint", debugEnv && debugEnv[0] == '1' ? colours[debugIndex % 3] : Vec3(1.0f));
  setEnvironment(m);
}

} // namespace

void registerGrassChunkMaterial(ecs::World &world, const char *name, const GrassLevel &level,
                                int levelIndex, float cornerX, float cornerZ, float seedOffset,
                                float padding) {
  auto &am = world.getResource<AssetManager>();
  am.registerMaterial<renderer::DiffuseMaterial>(name, "forest_grass", "white");
  auto &m = am.getMaterial(am.getMaterialID(name));
  configureGrass(am, m, kGrassChunkSide, 0.0f, level.chunkSize, Vec3(cornerX, cornerZ, seedOffset),
                 level.fadeInStart, level.fadeInEnd, level.fadeStart, level.fadeEnd,
                 level.bladeLod, levelIndex);
  m.setBoundsPadding(padding);
}

bool treeMidLods() {
  // Trees use their three baked 3D levels before the billboard;
  // FOREST_TREE_NO_MID_LODS goes from LOD0 straight to the billboard.
  static const bool on = std::getenv("FOREST_TREE_NO_MID_LODS") == nullptr;
  return on;
}

std::string meshName(const char *model, const char *part, int lod) {
  return std::string(model) + "_" + part + "_lod" + std::to_string(lod);
}

bool meshLoaded(const std::string &name) {
  return loadedMeshes().count(name) != 0;
}

void loadForestAssets(ecs::World &world, const ForestWorld &forest) {
  auto &am = world.getResource<AssetManager>();

  // --- Shaders ------------------------------------------------------------
  auto shader = [&](const char *vert, const char *frag, const char *name) {
    am.loadShader(assetPath(std::string("shaders/") + vert).c_str(),
                  assetPath(std::string("shaders/") + frag).c_str(), nullptr, name);
  };
  // The RenderSystem draws opaque batches in shader-id order, so this is the draw
  // order. The terrain goes after everything that stands on it: the trees, grass
  // and props write depth first, and the (expensive) terrain fragments they hide
  // are then rejected by the depth test before the shader runs.
  shader("wood.vert", "wood.frag", "forest_wood");
  shader("foliage.vert", "foliage.frag", "forest_foliage");
  shader("billboard.vert", "billboard.frag", "forest_billboard");
  shader("grass.vert", "grass.frag", "forest_grass");
  shader("terrain.vert", "terrain.frag", "forest_terrain");
  shader("water.vert", "water.frag", "forest_water");
  shader("sky.vert", "sky.frag", "forest_sky");
  // Keep this one last: the RenderSystem draws batches in shader-id order, and
  // the flare must come after everything it is drawn over.
  shader("flare.vert", "flare.frag", "forest_flare");

  // --- Textures -----------------------------------------------------------
  loadTexture(am, "ground_forest_albedo.jpg", "ground_forest_albedo", false, true, true, 16.0f);
  loadTexture(am, "ground_forest_normal.jpg", "ground_forest_normal", false, false, true, 16.0f);
  loadTexture(am, "ground_forest_arm.jpg", "ground_forest_arm", false, false, true, 16.0f);
  loadTexture(am, "ground_rock_albedo.jpg", "ground_rock_albedo", false, true, true, 16.0f);
  loadTexture(am, "ground_rock_normal.jpg", "ground_rock_normal", false, false, true, 16.0f);

  // The trees' and props' textures, meshes and materials (all but the ground and
  // grass) are left out of a FOREST_ONLY_GRASS scene.
  if (!onlyGrass()) {
    for (const char *set : {"island_trunk", "island_branches", "small_trunk", "small_branches",
                            "fir_branches", "deadlog", "stump", "boulder", "rockmoss"})
      loadWoodSet(am, set);
    for (const char *set : {"island_leaves", "small_leaves", "fir_twigs"})
      loadFoliage(am, set);
  }

  // --- Terrain ------------------------------------------------------------
  // PbrMaterial binds normal/metallic/roughness/ao to texture units 1-4; the
  // terrain shader reads them as: forest normal, forest ARM, rock albedo, rock normal.
  am.registerPbrMaterial<renderer::PbrMaterial>("terrain", "forest_terrain", "ground_forest_albedo",
                                                "ground_forest_normal", "ground_forest_arm",
                                                "ground_rock_albedo", "ground_rock_normal");
  {
    auto &m = am.getMaterial(am.getMaterialID("terrain"));
    m.setRenderState(opaque(false));
    m.setUniform("farGrass", grassLodMode() == 3 ? 1.0f : 0.0f);
    setEnvironment(m);
  }

  // --- Trees ---------------------------------------------------------------
  for (const TreeSpecies &s : kTreeSpecies) {
    if (onlyGrass()) break;
    ModelTextures t = treeTextures(s.name);
    loadMeshes(am, s.name, "trunk", treeMidLods() ? 2 : 0);
    loadMeshes(am, s.name, "leaf", treeMidLods() ? 2 : 0);
    registerWood(am, std::string(s.name) + "_trunk", t.trunk, 1.0f, 1.0f);
    if (s.hasBranches) {
      loadMeshes(am, s.name, "branch");
      registerWood(am, std::string(s.name) + "_branch", t.branch, 1.0f, 1.0f);
    }
    registerFoliage(am, std::string(s.name) + "_leaf", t.leaf, 1.0f,
                    Vec3(s.crownX, s.crownY, s.crownZ), s.crownRadius * 1.1f, s.crownBottom,
                    s.crownTop, 0.7f);
    registerBillboard(am, s);
  }

  // --- Props ----------------------------------------------------------------
  for (const PropModel &p : kPropModels) {
    if (onlyGrass()) break;
    loadMeshes(am, p.name, "body");
    registerWood(am, std::string(p.name) + "_body", propTexture(p.name), 0.0f, 0.0f);
  }

  // --- Grass ----------------------------------------------------------------
  // Two textures made from memory give the grass shader the ground: the
  // terrain's heights (float, so centimetres survive 70 m of relief) and the
  // baked mask (density, sun, sky). Nothing is filtered or mip-mapped in the
  // height map: the shader interpolates it itself, with the terrain mesh's own
  // triangle split.
  const Terrain &terrain = forest.terrain;
  {
    renderer::api::TextureDesc heights;
    heights.mipmaps = false;
    heights.minFilter = heights.magFilter = renderer::api::TextureFilter::Nearest;
    am.createTexture("terrain_heights", terrain.stride(), terrain.stride(),
                     renderer::api::TextureFormat::R32F, terrain.heights().data(), heights);

    renderer::api::TextureDesc mask;
    mask.mipmaps = false;  // sampled at (at most) its own resolution
    am.createTexture("grass_mask", cfg::kMaskCells, cfg::kMaskCells,
                     renderer::api::TextureFormat::RGBA8, terrain.grassMask.data(), mask);
  }

  const std::vector<GrassPool> pools = grassPools();
  for (size_t i = 0; i < pools.size(); ++i) {
    const GrassPool &pool = pools[i];
    am.addMesh((std::string(pool.name) + "_tuft").c_str(),
               buildGrassTuft(pool.blades, pool.radius, pool.widthMul));
    // The grass shader has no per-instance attributes, so any material class
    // will do; the shader reads everything it needs from the bindings below.
    am.registerMaterial<renderer::DiffuseMaterial>(pool.name, "forest_grass", "white");
    configureGrass(am, am.getMaterial(am.getMaterialID(pool.name)), pool.side, pool.period, 0.0f,
                   Vec3(0.0f), pool.fadeInStart, pool.fadeInEnd, pool.fadeStart, pool.fadeEnd,
                   pool.bladeLod, int(i));
  }

  // --- Water ----------------------------------------------------------------
  // Blended over the terrain and sky (the engine draws blended batches after
  // both), and without writing depth. It reads the terrain's heights to know how
  // deep the water is under each pixel: see water.frag.
  am.addMesh("water_plane", buildWaterPlane());
  am.registerMaterial<renderer::DiffuseMaterial>("water", "forest_water", "white");
  {
    auto &m = am.getMaterial(am.getMaterialID("water"));
    renderer::api::RenderState state;
    state.blend = renderer::api::BlendMode::Alpha;
    state.depthWrite = false;
    m.setRenderState(state);
    m.setTexture("heightMap", am.getTextureID("terrain_heights"));
    m.setUniform("worldHalf", 0.5f * cfg::kWorldSize);
    m.setUniform("heightCells", float(cfg::kCells));
    m.setUniform("skyColor", Vec3(0.20f, 0.42f, 0.88f));  // as the SkyboxComponent's
    m.setUniform("shallowColor", Vec3(0.05f, 0.34f, 0.36f));
    m.setUniform("deepColor", Vec3(0.004f, 0.045f, 0.15f));
    m.setUniform("skyAmbient", kSkyAmbient);
  }

  // --- Lens flare -----------------------------------------------------------
  // Sprites placed by the shader over the finished scene: no depth test, and
  // added to what is there rather than replacing it.
  am.addMesh("flare_quad", buildFlareQuad());
  am.registerMaterial<renderer::DiffuseMaterial>("lens_flare", "forest_flare", "white");
  {
    auto &m = am.getMaterial(am.getMaterialID("lens_flare"));
    renderer::api::RenderState state;
    state.cull = renderer::api::CullMode::None;
    state.depthTest = false;
    state.depthWrite = false;
    state.blend = renderer::api::BlendMode::Additive;
    m.setRenderState(state);
    m.setUniform("aspect", 16.0f / 9.0f);  // the engine letterboxes the view to 16:9
    m.setUniform("sunVisible", 0.0f);
  }

  LOG_INFO("Forest assets loaded");
}

} // namespace forest
