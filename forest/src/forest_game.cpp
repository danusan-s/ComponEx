#include "forest_game.hpp"

#include "billboard.hpp"
#include "components.hpp"
#include "forest_assets.hpp"
#include "forest_audio.hpp"
#include "forest_config.hpp"
#include "forest_world.hpp"
#include "frame_limiter.hpp"
#include "grass.hpp"
#include "lens_flare.hpp"
#include "player_system.hpp"
#include "screenshot.hpp"
#include "species_data.hpp"
#include "water.hpp"

#include "componeng/audio/audio_system.hpp"
#include "componeng/camera/camera_component.hpp"
#include "componeng/camera/camera_system.hpp"
#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/component/light_component.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/renderer/component/procedural_instances_component.hpp"
#include "componeng/renderer/component/skybox_component.hpp"
#include "componeng/renderer/culling/culling_system.hpp"
#include "componeng/renderer/lod/lod_component.hpp"
#include "componeng/renderer/render_system.hpp"
#include "componeng/resources/fog.hpp"
#include "componeng/utils/logger.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace forest {

using namespace componeng;
using core::Vec3;
using core::Vec4;

namespace {

struct LodRanges {
  float d[3];
};

struct TreeLod {
  float base, perScale, lod1;
};

// The LODs are chosen by how much of the screen's height an entity covers (see
// renderer::LodComponent). The thresholds below were set from the distances the
// levels used to switch at, for a tree or prop of `refScale` times its model
// size: those are the switch distances at that scale, and an entity bigger than
// that keeps each level further out, a smaller one gives it up sooner.
struct LodSizing {
  float size;      // the model's extent (its height, for a tree)
  float refScale;  // the scale the distance ranges are for
};

float screenHeightAt(const LodSizing &sizing, float distance) {
  return renderer::lodScreenHeight(sizing.size * sizing.refScale, distance, cfg::kFov);
}

// Spawns one mesh part of a model, with whichever LODs were baked.
void spawnPart(ecs::World &world, const char *model, const char *part,
               const char *material, const core::TransformComponent &tf,
               Vec4 color, LodRanges ranges, LodSizing sizing) {
  auto &am = world.getResource<renderer::AssetManager>();
  renderer::LodComponent lod;
  lod.size = sizing.size;
  for (int k = 0; k < 3; ++k) {
    std::string name = meshName(model, part, k);
    if (meshLoaded(name)) {
      lod.mesh[k] = am.getMeshID(name.c_str());
      lod.distance[k] = ranges.d[k];
      lod.screenHeight[k] = screenHeightAt(sizing, ranges.d[k]);
    }
  }
  if (lod.mesh[0] == 0)
    return;

  ecs::EntityID e = world.createEntity();
  world.addComponents(
      e, core::TransformComponent(tf),
      renderer::MeshComponent{.meshName = meshName(model, part, 0).c_str(),
                              .meshID = lod.mesh[0],
                              .visible = true},
      renderer::MaterialComponent{.materialName = material},
      renderer::ColorComponent{.color = color}, renderer::LodComponent(lod));
}

// The tree seen from far away: one quad, shown from `nearDistance` metres at
// the tree's reference scale (as a screen-height threshold; see BillboardLodComponent).
void spawnBillboard(ecs::World &world, const char *species,
                    const core::TransformComponent &tf, Vec4 color,
                    float nearDistance, LodSizing sizing) {
  auto &am = world.getResource<renderer::AssetManager>();
  std::string name = std::string(species) + "_billboard";
  ecs::EntityID e = world.createEntity();
  world.addComponents(
      e, core::TransformComponent(tf),
      renderer::MeshComponent{.meshName = name.c_str(),
                              .meshID = am.getMeshID(name.c_str()),
                              .visible = false},
      renderer::MaterialComponent{.materialName = name.c_str()},
      renderer::ColorComponent{.color = color},
      BillboardLodComponent{.nearDistance = nearDistance,
                            .size = sizing.size,
                            .screenHeight = screenHeightAt(sizing, nearDistance)});
}

void spawnTerrain(ecs::World &world, const Terrain &terrain) {
  auto &am = world.getResource<renderer::AssetManager>();
  const int chunks = cfg::kCells / cfg::kChunkCells;
  for (int cz = 0; cz < chunks; ++cz) {
    for (int cx = 0; cx < chunks; ++cx) {
      std::string name =
          "terrain_" + std::to_string(cx) + "_" + std::to_string(cz);
      am.addMesh(name.c_str(), terrain.buildChunk(cx, cz));
      ecs::EntityID e = world.createEntity();
      world.addComponents(
          e, core::TransformComponent{},
          renderer::MeshComponent{.meshName = name.c_str(),
                                  .meshID = am.getMeshID(name.c_str()),
                                  .visible = true},
          renderer::MaterialComponent{.materialName = "terrain"});
    }
  }
}

// One plane at sea level, out past the edge of the world.
void spawnWater(ecs::World &world) {
  auto &am = world.getResource<renderer::AssetManager>();
  ecs::EntityID e = world.createEntity();
  world.addComponents(
      e, core::TransformComponent{.position = Vec3(0.0f, cfg::kWaterLevel, 0.0f)},
      renderer::MeshComponent{.meshName = "water_plane",
                              .meshID = am.getMeshID("water_plane"),
                              .visible = true},
      renderer::MaterialComponent{.materialName = "water"});
}

void spawnTrees(ecs::World &world, const ForestWorld &fw) {
  for (const TreePlacement &t : fw.trees) {
    const TreeSpecies &s = kTreeSpecies[t.species];
    core::TransformComponent tf{.position = t.pos,
                                .rotation = Vec3(t.tiltX, t.yaw, t.tiltZ),
                                .scale = Vec3(t.scale)};
    float bark = 0.5f * (t.tint.x + t.tint.y);
    Vec4 woodColor(bark, bark, bark, t.sunVis);
    Vec4 leafColor(t.tint.x, t.tint.y, t.tint.z, t.sunVis);
    // Three 3D levels, then a billboard from where the last one ends.
    const float far = billboardNearDistance();
    // LOD0 holds to base + perScale * scale metres, LOD1 to `lod1`;
    // FOREST_TREE_LOD="base,perScale,lod1" overrides them (to tune the ranges).
    static const TreeLod lod = [] {
      TreeLod l{30.0f, 3.0f, 60.0f};
      if (const char *env = std::getenv("FOREST_TREE_LOD"))
        std::sscanf(env, "%f,%f,%f", &l.base, &l.perScale, &l.lod1);
      return l;
    }();
    LodRanges ranges{{std::min(lod.base + lod.perScale * t.scale, far), std::min(lod.lod1, far), far}};
    // LOD0 straight to the billboard, unless the middle levels are switched on.
    if (!treeMidLods())
      ranges = {{far, 0.0f, 0.0f}};

    std::string trunk = std::string(s.name) + "_trunk";
    std::string branch = std::string(s.name) + "_branch";
    std::string leaf = std::string(s.name) + "_leaf";
    // The distance ranges are for a tree of its species' mean scale.
    const LodSizing sizing{s.height, 0.5f * (s.minScale + s.maxScale)};
    spawnPart(world, s.name, "trunk", trunk.c_str(), tf, woodColor, ranges, sizing);
    // Twigs dropped for now (experiment): the separate branch mesh is not spawned.
    // if (s.hasBranches)
    //   spawnPart(world, s.name, "branch", branch.c_str(), tf, woodColor, ranges, sizing);
    (void)branch;
    spawnPart(world, s.name, "leaf", leaf.c_str(), tf, leafColor, ranges, sizing);
    spawnBillboard(world, s.name, tf, leafColor, far, sizing);
  }
}

void spawnProps(ecs::World &world, const ForestWorld &fw) {
  for (const PropPlacement &p : fw.props) {
    const PropModel &m = kPropModels[p.model];
    core::TransformComponent tf{.position = p.pos,
                                .rotation = p.hasEuler ? p.euler : Vec3(p.tiltX, p.yaw, p.tiltZ),
                                .scale = Vec3(p.scale)};
    LodRanges ranges{{60.0f, 170.0f, 0.0f}};
    // Logs and stumps are placed at about 0.8-1.5x, rocks at 0.3-2.2x: the
    // distance ranges are for a typical one.
    float refScale = 1.15f;
    std::string name = m.name;
    if (name == "boulder" || name.rfind("rock", 0) == 0) {
      ranges = {{80.0f, 260.0f, 0.0f}};
      refScale = 0.9f;
    }
    std::string material = name + "_body";
    const LodSizing sizing{std::max({m.height, m.extentX, m.extentZ}), refScale};
    spawnPart(world, m.name, "body", material.c_str(), tf,
              Vec4(p.tint, p.sunVis), ranges, sizing);
  }
}

// Each pool of grass is one entity drawing its tufts; the shader places each of
// them (see grass.vert). Culling is left off because the shader, not the mesh's
// bounds, decides where the tufts land. FOREST_NO_GRASS leaves the grass out
// (to measure what it costs).
void spawnGrass(ecs::World &world, const Terrain &terrain) {
  if (std::getenv("FOREST_NO_GRASS"))
    return;
  if (grassLodMode() == 3) {
    spawnGrassChunks(world, terrain);
    return;
  }
  for (const GrassPool &pool : grassPools()) {
    std::string mesh = std::string(pool.name) + "_tuft";
    ecs::EntityID field = world.createEntity();
    world.addComponents(
        field, core::TransformComponent{},
        renderer::MeshComponent{.meshName = mesh.c_str()},
        renderer::MaterialComponent{.materialName = pool.name},
        renderer::ProceduralInstancesComponent{.count = pool.count()});
  }
}

// The lens flare is one entity drawing kFlareElements screen-space sprites; the
// shader positions them from the sun's direction (see flare.vert).
void spawnLensFlare(ecs::World &world) {
  ecs::EntityID flare = world.createEntity();
  world.addComponents(
      flare, core::TransformComponent{},
      renderer::MeshComponent{.meshName = "flare_quad"},
      renderer::MaterialComponent{.materialName = "lens_flare"},
      renderer::ProceduralInstancesComponent{.count = kFlareElements});
}

} // namespace

void ForestGame::init(ecs::World &world) {
  world.registerComponents<PlayerComponent, BillboardLodComponent, GrassChunkComponent>();

  // Haze doubles as the sky's horizon colour, so distant ground fades into it.
  world.setResource(resources::Fog{.color = Vec4(0.55f, 0.68f, 0.86f, 1.0f),
                                   .density = 0.0070f,
                                   .start = 0.0f,
                                   .end = 1.0f,
                                   .enabled = true});

  LOG_INFO("Generating terrain, placing trees and baking lighting...");
  ForestWorld fw;
  fw.build();
  LOG_INFO("Placed %zu trees, %zu props, %zu obstacles", fw.trees.size(),
           fw.props.size(), fw.obstacles.size());

  loadForestAssets(world, fw);

  // Debug switches, to see what the grass costs: FOREST_NO_GRASS leaves it out,
  // FOREST_ONLY_GRASS leaves out the trees and props (see onlyGrass()).
  spawnTerrain(world, fw.terrain);
  spawnWater(world);
  if (!onlyGrass()) {
    spawnTrees(world, fw);
    spawnProps(world, fw);
  }
  spawnGrass(world, fw.terrain);
  spawnLensFlare(world);

  // --- Sun and sky ---------------------------------------------------------
  ecs::EntityID sun = world.createEntity();
  world.addComponents(sun, renderer::DirectionalLightComponent{
                               .direction = fw.sunDir,
                               .color = Vec3(1.0f, 0.86f, 0.64f) * 3.4f});

  ecs::EntityID sky = world.createEntity();
  world.addComponents(
      sky, renderer::SkyboxComponent{.shaderName = "forest_sky",
                                     .color = Vec3(0.20f, 0.42f, 0.88f),
                                     .horizonFromFog = true});

  // --- Player and camera -----------------------------------------------------
  ecs::EntityID cam = world.createEntity();
  world.addComponents(cam, core::TransformComponent{},
                      camera::CameraComponent{.fov = cfg::kFov,
                                              .aspectRatio = 16.0f / 9.0f,
                                              .nearPlane = 0.02f,
                                              .farPlane = 750.0f});
  world.setResource(camera::MainCamera{cam});

  // FOREST_SPAWN="x,z,yaw,pitch" starts the player somewhere else (for poking
  // at a particular spot without walking there).
  float spawnX = 0.0f, spawnZ = 0.0f, spawnYaw = 35.0f, spawnPitch = -4.0f;
  if (const char *env = std::getenv("FOREST_SPAWN"))
    std::sscanf(env, "%f,%f,%f,%f", &spawnX, &spawnZ, &spawnYaw, &spawnPitch);
  float spawnY = fw.terrain.heightAt(spawnX, spawnZ);
  PlayerComponent player;
  player.yaw = spawnYaw;
  player.pitch = spawnPitch;
  player.smoothY = spawnY;
  ecs::EntityID playerEntity = world.createEntity();
  world.addComponents(
      playerEntity,
      core::TransformComponent{.position = Vec3(spawnX, spawnY, spawnZ)},
      PlayerComponent(player));

  world.setResource(std::move(fw));

  setupForestAudio(world);

  world.registerSystem<PlayerSystem>(ecs::SystemGroup::Simulation);
  world.registerSystem<FootstepSystem>(ecs::SystemGroup::Simulation);
  world.registerSystem<LensFlareSystem>(ecs::SystemGroup::Simulation);
  world.registerSystem<BillboardLodSystem>(ecs::SystemGroup::Presentation);
  world.registerSystem<UnderwaterSystem>(ecs::SystemGroup::Presentation);
  world.registerSystem<GrassChunkSystem>(ecs::SystemGroup::Presentation);
  if (std::getenv("FOREST_SHOT")) {
    world.registerSystem<ScreenshotSystem>(ecs::SystemGroup::Presentation);
    world.addSystemDependencies<ScreenshotSystem, renderer::RenderSystem>();
  }
  // Last thing in the frame: waits out whatever is left of the frame budget.
  world.registerSystem<FrameLimiterSystem>(ecs::SystemGroup::Presentation);
  world.addSystemDependencies<FrameLimiterSystem, renderer::RenderSystem>();
  // The camera's matrices must be built from where the player ended up this
  // frame, not last frame.
  world.addSystemDependencies<camera::CameraSystem, PlayerSystem>();
  // The footsteps follow what the player did this frame, and the audio system
  // applies them.
  world.addSystemDependencies<FootstepSystem, PlayerSystem>();
  world.addSystemDependencies<audio::AudioSystem, FootstepSystem>();
  // The flare reads where the player ended up this frame.
  world.addSystemDependencies<LensFlareSystem, PlayerSystem>();
  // Culling must see which billboards were just shown or hidden.
  world.addSystemDependencies<renderer::CullingSystem, BillboardLodSystem>();
  // The fog, sky and sun must be set for this frame's camera before they are drawn.
  world.addSystemDependencies<renderer::RenderSystem, UnderwaterSystem>();
  world.addSystemDependencies<renderer::CullingSystem, GrassChunkSystem>();
}

void ForestGame::shutdown(ecs::World &) {
}

} // namespace forest
