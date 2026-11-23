#include "terrain.hpp"

#include "forest_config.hpp"
#include "noise.hpp"

#include "componeng/renderer/asset/mesh.hpp"

#include <algorithm>
#include <cmath>

namespace forest {

using componeng::core::Vec3;

namespace {

float rawHeight(float x, float z) {
  using namespace noise;
  float h = 0.0f;
  // Broad rolling hills, a few sharper ridges on top, then knolls and micro
  // relief so the ground is never smooth at walking scale.
  h += (fbm(x * 0.0032f + 17.0f, z * 0.0032f + 3.0f, 4, 1) - 0.35f) * 46.0f;
  h += ridged(x * 0.0075f, z * 0.0075f, 3, 2) * 13.0f;
  h += (fbm(x * 0.028f, z * 0.028f, 4, 3) - 0.5f) * 5.0f;
  h += (fbm(x * 0.11f, z * 0.11f, 3, 4) - 0.5f) * 0.9f;
  return h;
}

float finalHeight(float x, float z, float spawnHeight) {
  float h = rawHeight(x, z);
  // A gentle meadow around the spawn point.
  float r = std::sqrt(x * x + z * z);
  h = spawnHeight + (h - spawnHeight) * noise::smoothstep(10.0f, 55.0f, r);
  // The land is an island: it falls away into the sea towards the edge. The
  // distance is wobbled by low-frequency noise so the coast is irregular rather
  // than a circle, and the fall is complete (e = 1) before the world's border
  // (320 m on the axes, at worst about 285 m after the wobble), so the mesh
  // ends deep under the water.
  float coast = r + (noise::fbm(x * 0.012f - 31.0f, z * 0.012f + 8.0f, 3, 7) - 0.5f) * 70.0f;
  float e = noise::smoothstep(0.27f * cfg::kWorldSize, 0.44f * cfg::kWorldSize, coast);
  return h + (cfg::kWaterLevel - 10.0f - h) * e;
}

} // namespace

void Terrain::generate() {
  m_half = cfg::kWorldSize * 0.5f;
  m_cell = cfg::kWorldSize / float(cfg::kCells);
  m_stride = cfg::kCells + 1;
  m_height.resize(size_t(m_stride) * m_stride);

  float spawn = rawHeight(0.0f, 0.0f);
  for (int j = 0; j < m_stride; ++j) {
    for (int i = 0; i < m_stride; ++i) {
      m_height[index(i, j)] =
          finalHeight(-m_half + i * m_cell, -m_half + j * m_cell, spawn);
    }
  }

  size_t n = m_height.size();
  sunVis.assign(n, 1.0f);
  skyVis.assign(n, 1.0f);
}

float Terrain::heightAt(float x, float z) const {
  float u = (x + m_half) / m_cell, v = (z + m_half) / m_cell;
  u = std::clamp(u, 0.0f, float(cfg::kCells) - 1e-3f);
  v = std::clamp(v, 0.0f, float(cfg::kCells) - 1e-3f);
  int i = int(u), j = int(v);
  float tx = u - float(i), tz = v - float(j);
  float h00 = m_height[index(i, j)], h10 = m_height[index(i + 1, j)];
  float h01 = m_height[index(i, j + 1)], h11 = m_height[index(i + 1, j + 1)];
  // The mesh splits each quad along the (i+1, j) - (i, j+1) diagonal.
  if (tx + tz <= 1.0f) return h00 + tx * (h10 - h00) + tz * (h01 - h00);
  return h11 + (1.0f - tx) * (h01 - h11) + (1.0f - tz) * (h10 - h11);
}

Vec3 Terrain::normalAt(float x, float z) const {
  float e = m_cell;
  float dx = heightAt(x - e, z) - heightAt(x + e, z);
  float dz = heightAt(x, z - e) - heightAt(x, z + e);
  return componeng::core::normalize(Vec3(dx, 2.0f * e, dz));
}

float Terrain::sample(const std::vector<float> &layer, float x, float z) const {
  float u = (x + m_half) / m_cell, v = (z + m_half) / m_cell;
  u = std::clamp(u, 0.0f, float(cfg::kCells) - 1e-3f);
  v = std::clamp(v, 0.0f, float(cfg::kCells) - 1e-3f);
  int i = int(u), j = int(v);
  float tx = u - float(i), tz = v - float(j);
  float a = layer[index(i, j)], b = layer[index(i + 1, j)];
  float c = layer[index(i, j + 1)], d = layer[index(i + 1, j + 1)];
  return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

std::unique_ptr<componeng::renderer::Mesh> Terrain::buildChunk(int cx, int cz) const {
  const int n = cfg::kChunkCells;
  const int i0 = cx * n, j0 = cz * n;

  auto mesh = std::make_unique<componeng::renderer::Mesh>();
  mesh->m_vertices.reserve(size_t(n + 1) * (n + 1) * 8);
  mesh->m_indices.reserve(size_t(n) * n * 6);

  auto clampi = [&](int v) { return std::clamp(v, 0, m_stride - 1); };
  for (int j = 0; j <= n; ++j) {
    for (int i = 0; i <= n; ++i) {
      int gi = i0 + i, gj = j0 + j;
      // Central differences over the global grid, so chunk borders share
      // normals and there is no visible seam.
      float dx = m_height[index(clampi(gi - 1), gj)] - m_height[index(clampi(gi + 1), gj)];
      float dz = m_height[index(gi, clampi(gj - 1))] - m_height[index(gi, clampi(gj + 1))];
      Vec3 nrm = componeng::core::normalize(Vec3(dx, 2.0f * m_cell, dz));
      size_t idx = size_t(index(gi, gj));
      float vtx[8] = {-m_half + gi * m_cell, m_height[idx], -m_half + gj * m_cell,
                      nrm.x, nrm.y, nrm.z, sunVis[idx], skyVis[idx]};
      mesh->m_vertices.insert(mesh->m_vertices.end(), vtx, vtx + 8);
    }
  }
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      unsigned a = unsigned(j * (n + 1) + i), b = a + 1;
      unsigned c = a + unsigned(n + 1), d = c + 1;
      // a-c-b and b-c-d, counter-clockwise seen from above.
      unsigned tri[6] = {a, c, b, b, c, d};
      mesh->m_indices.insert(mesh->m_indices.end(), tri, tri + 6);
    }
  }
  return mesh;
}

} // namespace forest
