#include "forest_assets.hpp"

#include "forest_config.hpp"
#include "forest_world.hpp"
#include "grass.hpp"
#include "species_data.hpp"

#include "componeng/renderer/asset/diffuse.hpp"
#include "componeng/renderer/asset/pbr.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/utils/logger.hpp"

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
  if (!std::strncmp(name, "rock", 4)) return "rockmoss";
  if (!std::strncmp(name, "fern", 4)) return "fern";
  return "shrub";
}

std::unordered_set<std::string> &loadedMeshes() {
  static std::unordered_set<std::string> names;
  return names;
}

// Not every part has every level of detail (branches only exist up close), so
// load whichever files the converter wrote.
void loadMeshes(AssetManager &am, const char *model, const char *part) {
  for (int lod = 0; lod < 3; ++lod) {
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
                     float crownBlend, float groundCover) {
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
  m.setUniform("groundCover", groundCover);
  m.setBoundsPadding(1.5f);
  setEnvironment(m);
}

} // namespace

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
  shader("terrain.vert", "terrain.frag", "forest_terrain");
  shader("wood.vert", "wood.frag", "forest_wood");
  shader("foliage.vert", "foliage.frag", "forest_foliage");
  shader("grass.vert", "grass.frag", "forest_grass");
  shader("sky.vert", "sky.frag", "forest_sky");

  // --- Textures -----------------------------------------------------------
  loadTexture(am, "ground_forest_albedo.jpg", "ground_forest_albedo", false, true, true, 16.0f);
  loadTexture(am, "ground_forest_normal.jpg", "ground_forest_normal", false, false, true, 16.0f);
  loadTexture(am, "ground_forest_arm.jpg", "ground_forest_arm", false, false, true, 16.0f);
  loadTexture(am, "ground_rock_albedo.jpg", "ground_rock_albedo", false, true, true, 16.0f);
  loadTexture(am, "ground_rock_normal.jpg", "ground_rock_normal", false, false, true, 16.0f);

  for (const char *set : {"island_trunk", "island_branches", "small_trunk", "small_branches",
                          "fir_branches", "deadlog", "stump", "boulder", "rockmoss"})
    loadWoodSet(am, set);
  for (const char *set : {"island_leaves", "small_leaves", "fir_twigs", "fern", "shrub"})
    loadFoliage(am, set);

  // --- Terrain ------------------------------------------------------------
  // PbrMaterial binds normal/metallic/roughness/ao to texture units 1-4; the
  // terrain shader reads them as: forest normal, forest ARM, rock albedo, rock normal.
  am.registerPbrMaterial<renderer::PbrMaterial>("terrain", "forest_terrain", "ground_forest_albedo",
                                                "ground_forest_normal", "ground_forest_arm",
                                                "ground_rock_albedo", "ground_rock_normal");
  {
    auto &m = am.getMaterial(am.getMaterialID("terrain"));
    m.setRenderState(opaque(false));
    setEnvironment(m);
  }

  // --- Trees ---------------------------------------------------------------
  for (const TreeSpecies &s : kTreeSpecies) {
    ModelTextures t = treeTextures(s.name);
    loadMeshes(am, s.name, "trunk");
    loadMeshes(am, s.name, "leaf");
    registerWood(am, std::string(s.name) + "_trunk", t.trunk, 1.0f, 1.0f);
    if (s.hasBranches) {
      loadMeshes(am, s.name, "branch");
      registerWood(am, std::string(s.name) + "_branch", t.branch, 1.0f, 1.0f);
    }
    registerFoliage(am, std::string(s.name) + "_leaf", t.leaf, 1.0f,
                    Vec3(s.crownX, s.crownY, s.crownZ), s.crownRadius * 1.1f, s.crownBottom,
                    s.crownTop, 0.7f, 0.0f);
  }

  // --- Props ----------------------------------------------------------------
  for (const PropModel &p : kPropModels) {
    const char *tex = propTexture(p.name);
    loadMeshes(am, p.name, "body");
    if (p.foliage) {
      bool fern = !std::strncmp(p.name, "fern", 4);
      float r = 0.5f * (p.extentX > p.extentZ ? p.extentX : p.extentZ);
      registerFoliage(am, std::string(p.name) + "_body", tex, fern ? 0.5f : 0.3f,
                      Vec3(0.0f, p.height * 0.5f, 0.0f), r, 0.0f, p.height, fern ? 0.2f : 0.35f, 1.0f);
    } else {
      registerWood(am, std::string(p.name) + "_body", tex, 0.0f, 0.0f);
    }
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

  am.addMesh("grass_tuft", buildGrassTuft());
  // The grass shader has no per-instance attributes, so any material class
  // will do; the shader reads everything it needs from the bindings below.
  am.registerMaterial<renderer::DiffuseMaterial>("grass", "forest_grass", "white");
  {
    auto &m = am.getMaterial(am.getMaterialID("grass"));
    m.setRenderState(opaque(true));
    m.setTexture("heightMap", am.getTextureID("terrain_heights"));
    m.setTexture("grassMask", am.getTextureID("grass_mask"));
    m.setUniform("worldHalf", terrain.half());
    m.setUniform("heightCells", float(cfg::kCells));
    m.setUniform("tuftSide", float(GrassField::kTuftSide));
    m.setUniform("period", GrassField::kPeriod);
    m.setUniform("fadeStart", GrassField::kFadeStart);
    m.setUniform("fadeEnd", GrassField::kFadeEnd);
    setEnvironment(m);
  }

  LOG_INFO("Forest assets loaded");
}

} // namespace forest
