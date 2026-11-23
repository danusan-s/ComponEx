#include "grass.hpp"

#include "forest_config.hpp"
#include "terrain.hpp"

#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/renderer/component/procedural_instances_component.hpp"
#include "forest_assets.hpp"
#include "componeng/utils/logger.hpp"
#include "componeng/renderer/asset/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <string>

namespace forest {

using componeng::core::Vec3;

std::unique_ptr<componeng::renderer::Mesh> buildGrassTuft(int blades, float radius, float widthMul) {
  const int kBlades = blades;
  constexpr float kTau = 6.2831853f;
  // Rows along the blade: three quads' worth of width, then a point.
  constexpr float rows[3] = {0.0f, 0.34f, 0.68f};

  std::mt19937 gen(1234);
  auto uni = [&] { return std::uniform_real_distribution<float>(0.0f, 1.0f)(gen); };

  auto mesh = std::make_unique<componeng::renderer::Mesh>();
  auto vert = [&](Vec3 p, Vec3 n, float u, float v) {
    float d[8] = {p.x, p.y, p.z, n.x, n.y, n.z, u, v};
    mesh->m_vertices.insert(mesh->m_vertices.end(), d, d + 8);
    return unsigned(mesh->m_vertices.size() / 8 - 1);
  };

  for (int b = 0; b < kBlades; ++b) {
    float ang = uni() * kTau, rad = std::sqrt(uni()) * radius;
    Vec3 root(std::cos(ang) * rad, 0.0f, std::sin(ang) * rad);
    float height = 0.5f + uni() * 0.55f;
    float width = (0.030f + uni() * 0.018f) * widthMul;

    // The blade faces a random way; it leans away from the tuft's centre.
    float face = uni() * kTau;
    Vec3 nrm(std::cos(face), 0.35f, std::sin(face));
    nrm = componeng::core::normalize(nrm);
    Vec3 axis(-std::sin(face), 0.0f, std::cos(face));
    float outward = std::atan2(root.z, root.x);
    float leanDir = outward + (uni() - 0.5f) * 1.6f;
    float lean = (0.10f + uni() * 0.30f) * height;

    unsigned idx[3][2];
    for (int r = 0; r < 3; ++r) {
      float v = rows[r];
      float w = width * (1.0f - v * 0.7f);
      Vec3 c = root + Vec3(std::cos(leanDir), 0.0f, std::sin(leanDir)) * (lean * v * v) +
               Vec3(0.0f, height * v, 0.0f);
      idx[r][0] = vert(c - axis * w, nrm, float(b), v);
      idx[r][1] = vert(c + axis * w, nrm, float(b) + 0.9f, v);
    }
    Vec3 tip = root + Vec3(std::cos(leanDir), 0.0f, std::sin(leanDir)) * lean +
               Vec3(0.0f, height, 0.0f);
    unsigned t = vert(tip, nrm, float(b) + 0.45f, 1.0f);

    auto &ix = mesh->m_indices;
    for (int r = 0; r < 2; ++r) {
      unsigned l0 = idx[r][0], r0 = idx[r][1], l1 = idx[r + 1][0], r1 = idx[r + 1][1];
      unsigned quad[6] = {l0, r0, r1, l0, r1, l1};
      ix.insert(ix.end(), quad, quad + 6);
    }
    unsigned cap[3] = {idx[2][0], idx[2][1], t};
    ix.insert(ix.end(), cap, cap + 3);
  }
  return mesh;
}

int grassLodMode() {
  static const int mode = [] {
    const char *env = std::getenv("FOREST_GRASS_LOD");
    return env ? std::atoi(env) : 3;
  }();
  return mode;
}

std::vector<GrassPool> grassPools() {
  if (grassLodMode() == 3) return {};  // chunks instead (grassLevels)
  return {{"grass", 12, 0.22f, 1.0f, 144.0f, 432, 0.0f, 0.0f, 48.0f, 70.0f, 0.0f}};
}

std::vector<GrassLevel> grassLevels() {
  if (grassLodMode() != 3) return {};
  // Ring 0 is the full tuft, its blades dropping out with distance (grass.vert).
  // Rings 1 and 2 are 4x and 16x sparser, with wider, coarser tufts. Each ring is
  // at full size before the one inside it has thinned out, so two rings never
  // fade together and leave a sparse band between them. The ranges are what the
  // grass could afford: shortening them by 20% took a quarter off its cost.
  // The distances below are the original ones, scaled by 0.85 by default;
  // FOREST_GRASS_RANGE replaces that factor (1 = the original ranges).
  static const float k = [] {
    const char *env = std::getenv("FOREST_GRASS_RANGE");
    return env ? float(std::atof(env)) : 0.85f;
  }();
  return {{"grass_c0", 16.0f, 12, 0.22f, 1.0f, 0.0f, 0.0f, 48.0f * k, 58.0f * k, 1.0f},
          {"grass_c1", 32.0f, 8, 0.45f, 2.2f, 24.0f * k, 37.0f * k, 80.0f * k, 93.0f * k, 0.0f},
          {"grass_c2", 64.0f, 10, 0.9f, 4.0f, 67.0f * k, 80.0f * k, 128.0f * k, 144.0f * k, 0.0f}};
}

void GrassChunkSystem::onUpdate(const componeng::ecs::SystemState &state) {
  using namespace componeng;
  ecs::World &world = *state.world;
  const core::Vec3 cam = world
                             .getComponent<core::TransformComponent>(
                                 world.getResource<camera::MainCamera>().entity)
                             .position;

  world.query()
      .read<core::TransformComponent>()
      .write<renderer::MeshComponent>()
      .read<GrassChunkComponent>()
      .eachParallel(world.threadPool(),
                    [cam](const core::TransformComponent &t, renderer::MeshComponent &mesh,
                          const GrassChunkComponent &chunk) {
                      const float ax = std::fabs(cam.x - t.position.x);
                      const float az = std::fabs(cam.z - t.position.z);
                      // Distance to the nearest and the farthest point of the square.
                      const float nx = std::max(ax - chunk.half, 0.0f);
                      const float nz = std::max(az - chunk.half, 0.0f);
                      const float fx = ax + chunk.half, fz = az + chunk.half;
                      mesh.visible = nx * nx + nz * nz < chunk.fadeEnd * chunk.fadeEnd &&
                                     fx * fx + fz * fz > chunk.fadeInStart * chunk.fadeInStart;
                    });
}

void spawnGrassChunks(componeng::ecs::World &world, const Terrain &terrain) {
  using namespace componeng;
  auto &am = world.getResource<renderer::AssetManager>();
  const float half = terrain.half();
  const float texel = cfg::kWorldSize / float(cfg::kMaskCells);
  constexpr float kTuftsPerChunk = float(kGrassChunkSide * kGrassChunkSide);

  const std::vector<GrassLevel> levels = grassLevels();
  float seedBase = 0.0f;  // each chunk of each ring gets its own run of random numbers
  int made = 0, empty = 0;
  for (size_t li = 0; li < levels.size(); ++li) {
    const GrassLevel &level = levels[li];
    const float size = level.chunkSize;
    const std::string mesh = std::string(level.name) + "_tuft";
    am.addMesh(mesh.c_str(), buildGrassTuft(level.blades, level.radius, level.widthMul));

    const int n = int(std::ceil(2.0f * half / size));
    for (int cz = 0; cz < n; ++cz) {
      for (int cx = 0; cx < n; ++cx) {
        const float seed = seedBase + float(cz * n + cx) * kTuftsPerChunk;
        const float x0 = -half + float(cx) * size, z0 = -half + float(cz) * size;

        // Where the chunk's ground is, for its bounds; and whether anything grows on it.
        float lo = 1e9f, hi = -1e9f;
        for (float z = 0.0f; z <= size + 0.01f; z += 2.0f)
          for (float x = 0.0f; x <= size + 0.01f; x += 2.0f) {
            float h = terrain.heightAt(x0 + x, z0 + z);
            lo = std::min(lo, h);
            hi = std::max(hi, h);
          }
        const int i0 = std::clamp(int(std::floor((x0 + half) / texel)), 0, cfg::kMaskCells - 1);
        const int i1 = std::clamp(int(std::ceil((x0 + size + half) / texel)), 1, cfg::kMaskCells);
        const int j0 = std::clamp(int(std::floor((z0 + half) / texel)), 0, cfg::kMaskCells - 1);
        const int j1 = std::clamp(int(std::ceil((z0 + size + half) / texel)), 1, cfg::kMaskCells);
        bool grows = false;
        for (int j = j0; j < j1 && !grows; ++j)
          for (int i = i0; i < i1; ++i)
            if (terrain.grassMask[(size_t(j) * cfg::kMaskCells + size_t(i)) * 4] != 0) {
              grows = true;
              break;
            }
        if (!grows) {
          ++empty;
          continue;
        }

        // The engine culls on the tuft mesh's bounds, moved to the chunk's centre and
        // grown by this: sideways to the chunk's edge plus how far a tuft reaches over
        // it, and up and down over the ground's relief plus a tuft's height.
        const float padding = std::max(0.5f * size + level.radius * 1.6f + 1.5f,
                                       0.5f * (hi - lo) + 2.5f);

        const std::string name = std::string(level.name) + "_" + std::to_string(cx) + "_" +
                                 std::to_string(cz);
        registerGrassChunkMaterial(world, name.c_str(), level, int(li), x0, z0, seed, padding);

        ecs::EntityID e = world.createEntity();
        world.addComponents(
            e, core::TransformComponent{.position = core::Vec3(x0 + 0.5f * size, 0.5f * (lo + hi),
                                                               z0 + 0.5f * size)},
           
            renderer::MeshComponent{.meshName = mesh.c_str(), .visible = false},
            renderer::MaterialComponent{.materialName = name.c_str()},
            renderer::ProceduralInstancesComponent{.count = kGrassChunkSide * kGrassChunkSide,
                                                   .cull = true},
            GrassChunkComponent{.half = 0.5f * size,
                                .fadeInStart = level.fadeInStart,
                                .fadeEnd = level.fadeEnd});
        ++made;
      }
    }
    seedBase += float(n * n) * kTuftsPerChunk;
  }
  LOG_INFO("Grass: %d chunks (%d without grass skipped)", made, empty);
}

} // namespace forest
