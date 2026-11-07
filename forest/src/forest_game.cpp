#include "forest_game.hpp"

#include "components.hpp"
#include "forest_assets.hpp"
#include "forest_config.hpp"
#include "forest_world.hpp"
#include "grass.hpp"
#include "lens_flare.hpp"
#include "player_system.hpp"
#include "species_data.hpp"

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
#include "componeng/renderer/lod/lod_component.hpp"
#include "componeng/resources/fog.hpp"
#include "componeng/utils/logger.hpp"

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

// Spawns one mesh part of a model, with whichever LODs were baked.
void spawnPart(ecs::World &world, const char *model, const char *part, const char *material,
               const core::TransformComponent &tf, Vec4 color, LodRanges ranges) {
  auto &am = world.getResource<renderer::AssetManager>();
  renderer::LodComponent lod;
  for (int k = 0; k < 3; ++k) {
    std::string name = meshName(model, part, k);
    if (meshLoaded(name)) {
      lod.mesh[k] = am.getMeshID(name.c_str());
      lod.distance[k] = ranges.d[k];
    }
  }
  if (lod.mesh[0] == 0) return;

  ecs::EntityID e = world.createEntity();
  world.addComponents(
      e, core::TransformComponent(tf),
      renderer::MeshComponent{.meshName = meshName(model, part, 0).c_str(),
                              .meshID = lod.mesh[0],
                              .visible = true},
      renderer::MaterialComponent{.materialName = material},
      renderer::ColorComponent{.color = color}, renderer::LodComponent(lod));
}

void spawnTerrain(ecs::World &world, const Terrain &terrain) {
  auto &am = world.getResource<renderer::AssetManager>();
  const int chunks = cfg::kCells / cfg::kChunkCells;
  for (int cz = 0; cz < chunks; ++cz) {
    for (int cx = 0; cx < chunks; ++cx) {
      std::string name = "terrain_" + std::to_string(cx) + "_" + std::to_string(cz);
      am.addMesh(name.c_str(), terrain.buildChunk(cx, cz));
      ecs::EntityID e = world.createEntity();
      world.addComponents(e, core::TransformComponent{},
                          renderer::MeshComponent{.meshName = name.c_str(),
                                                  .meshID = am.getMeshID(name.c_str()),
                                                  .visible = true},
                          renderer::MaterialComponent{.materialName = "terrain"});
    }
  }
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
    LodRanges ranges{{55.0f + 6.0f * t.scale, 140.0f, 330.0f}};

    std::string trunk = std::string(s.name) + "_trunk";
    std::string branch = std::string(s.name) + "_branch";
    std::string leaf = std::string(s.name) + "_leaf";
    spawnPart(world, s.name, "trunk", trunk.c_str(), tf, woodColor, ranges);
    if (s.hasBranches) spawnPart(world, s.name, "branch", branch.c_str(), tf, woodColor, ranges);
    spawnPart(world, s.name, "leaf", leaf.c_str(), tf, leafColor, ranges);
  }
}

void spawnProps(ecs::World &world, const ForestWorld &fw) {
  for (const PropPlacement &p : fw.props) {
    const PropModel &m = kPropModels[p.model];
    core::TransformComponent tf{.position = p.pos,
                                .rotation = Vec3(p.tiltX, p.yaw, p.tiltZ),
                                .scale = Vec3(p.scale)};
    LodRanges ranges{{60.0f, 170.0f, 0.0f}};
    std::string name = m.name;
    if (name == "boulder" || name.rfind("rock", 0) == 0) ranges = {{80.0f, 260.0f, 0.0f}};
    std::string material = name + "_body";
    spawnPart(world, m.name, "body", material.c_str(), tf, Vec4(p.tint, p.sunVis), ranges);
  }
}

// All the grass is one entity drawing GrassField::kTuftCount tufts; the shader
// places each of them (see grass.vert). Culling is left off because the shader,
// not the mesh's bounds, decides where the tufts land.
void spawnGrass(ecs::World &world) {
  ecs::EntityID field = world.createEntity();
  world.addComponents(field, core::TransformComponent{},
                      renderer::MeshComponent{.meshName = "grass_tuft"},
                      renderer::MaterialComponent{.materialName = "grass"},
                      renderer::ProceduralInstancesComponent{.count = GrassField::kTuftCount});
}

// The lens flare is one entity drawing kFlareElements screen-space sprites; the
// shader positions them from the sun's direction (see flare.vert).
void spawnLensFlare(ecs::World &world) {
  ecs::EntityID flare = world.createEntity();
  world.addComponents(flare, core::TransformComponent{},
                      renderer::MeshComponent{.meshName = "flare_quad"},
                      renderer::MaterialComponent{.materialName = "lens_flare"},
                      renderer::ProceduralInstancesComponent{.count = kFlareElements});
}

} // namespace

void ForestGame::init(ecs::World &world) {
  world.registerComponents<PlayerComponent>();

  // Haze doubles as the sky's horizon colour, so distant ground fades into it.
  world.setResource(resources::Fog{.color = Vec4(0.55f, 0.68f, 0.86f, 1.0f),
                                   .density = 0.0070f,
                                   .start = 0.0f,
                                   .end = 1.0f,
                                   .enabled = true});

  LOG_INFO("Generating terrain, placing trees and baking lighting...");
  ForestWorld fw;
  fw.build();
  LOG_INFO("Placed %zu trees, %zu props, %zu obstacles", fw.trees.size(), fw.props.size(),
           fw.obstacles.size());

  loadForestAssets(world, fw);

  spawnTerrain(world, fw.terrain);
  spawnTrees(world, fw);
  spawnProps(world, fw);
  spawnGrass(world);
  spawnLensFlare(world);

  // --- Sun and sky ---------------------------------------------------------
  ecs::EntityID sun = world.createEntity();
  world.addComponents(sun, renderer::DirectionalLightComponent{
                               .direction = fw.sunDir, .color = Vec3(1.0f, 0.86f, 0.64f) * 3.4f});

  ecs::EntityID sky = world.createEntity();
  world.addComponents(sky, renderer::SkyboxComponent{.shaderName = "forest_sky",
                                                     .color = Vec3(0.20f, 0.42f, 0.88f),
                                                     .horizonFromFog = true});

  // --- Player and camera -----------------------------------------------------
  ecs::EntityID cam = world.createEntity();
  world.addComponents(cam, core::TransformComponent{},
                      camera::CameraComponent{.fov = 70.0f,
                                              .aspectRatio = 16.0f / 9.0f,
                                              .nearPlane = 0.12f,
                                              .farPlane = 450.0f});
  world.setResource(camera::MainCamera{cam});

  // FOREST_SPAWN="x,z,yaw,pitch" starts the player somewhere else (for poking at
  // a particular spot without walking there).
  float spawnX = 0.0f, spawnZ = 0.0f, spawnYaw = 35.0f, spawnPitch = -4.0f;
  if (const char *env = std::getenv("FOREST_SPAWN"))
    std::sscanf(env, "%f,%f,%f,%f", &spawnX, &spawnZ, &spawnYaw, &spawnPitch);
  float spawnY = fw.terrain.heightAt(spawnX, spawnZ);
  PlayerComponent player;
  player.yaw = spawnYaw;
  player.pitch = spawnPitch;
  player.smoothY = spawnY;
  ecs::EntityID playerEntity = world.createEntity();
  world.addComponents(playerEntity, core::TransformComponent{.position = Vec3(spawnX, spawnY, spawnZ)},
                      PlayerComponent(player));

  world.setResource(std::move(fw));

  world.registerSystem<PlayerSystem>(ecs::SystemGroup::Simulation);
  world.registerSystem<LensFlareSystem>(ecs::SystemGroup::Simulation);
  // The camera's matrices must be built from where the player ended up this
  // frame, not last frame.
  world.addSystemDependencies<camera::CameraSystem, PlayerSystem>();
  // The flare reads where the player ended up this frame.
  world.addSystemDependencies<LensFlareSystem, PlayerSystem>();
}

void ForestGame::shutdown(ecs::World &) {
}

} // namespace forest
