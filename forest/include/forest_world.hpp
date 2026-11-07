#pragma once

#include "terrain.hpp"

#include "componeng/core/types.hpp"

#include <cmath>
#include <vector>

namespace forest {

// A vertical cylinder the player cannot walk through.
struct Obstacle {
  float x, z, r;
  float top;      // height of its upper surface above the ground it stands on
  bool walkable;  // the top is flat enough to stand on (stumps)
  float baseY;    // ground height at its centre
};

// Uniform grid over the xz plane, so the player only tests the few obstacles
// that are actually nearby instead of every tree in the world.
class ObstacleGrid {
public:
  void init(float half) {
    m_half = half;
    m_n = int(std::ceil(2.0f * half / kCell));
    m_cells.assign(size_t(m_n) * m_n, {});
    m_all.clear();
  }

  void add(const Obstacle &o) {
    m_all.push_back(o);
    int id = int(m_all.size()) - 1;
    int i0, j0, i1, j1;
    range(o.x - o.r, o.z - o.r, o.x + o.r, o.z + o.r, i0, j0, i1, j1);
    for (int j = j0; j <= j1; ++j)
      for (int i = i0; i <= i1; ++i) m_cells[size_t(j) * m_n + i].push_back(id);
  }

  template <class F> void query(float x, float z, float r, F &&fn) const {
    int i0, j0, i1, j1;
    range(x - r, z - r, x + r, z + r, i0, j0, i1, j1);
    for (int j = j0; j <= j1; ++j)
      for (int i = i0; i <= i1; ++i)
        for (int id : m_cells[size_t(j) * m_n + i]) fn(m_all[size_t(id)]);
  }

  size_t size() const { return m_all.size(); }

private:
  static constexpr float kCell = 8.0f;
  void range(float x0, float z0, float x1, float z1, int &i0, int &j0, int &i1, int &j1) const {
    auto cell = [&](float v) {
      int c = int(std::floor((v + m_half) / kCell));
      return c < 0 ? 0 : (c >= m_n ? m_n - 1 : c);
    };
    i0 = cell(x0);
    i1 = cell(x1);
    j0 = cell(z0);
    j1 = cell(z1);
  }
  float m_half = 0.0f;
  int m_n = 0;
  std::vector<Obstacle> m_all;
  std::vector<std::vector<int>> m_cells;
};

struct TreePlacement {
  int species;  // index into kTreeSpecies
  componeng::core::Vec3 pos;
  float scale, yaw, tiltX, tiltZ;
  componeng::core::Vec3 tint;
  float sunVis = 1.0f;  // baked: how much sun reaches the crown through other trees
};

struct PropPlacement {
  int model;  // index into kPropModels
  componeng::core::Vec3 pos;
  float scale, yaw, tiltX, tiltZ;
  componeng::core::Vec3 tint;
  float sunVis = 1.0f;
};

// Everything the game knows about the generated world, kept as an ECS
// resource: the terrain, where things were planted, what blocks the player.
struct ForestWorld {
  Terrain terrain;
  ObstacleGrid obstacles;
  std::vector<TreePlacement> trees;
  std::vector<PropPlacement> props;
  componeng::core::Vec3 sunDir;  // unit, towards the sun

  void build();
};

// Index of a named model in kPropModels, or -1.
int propIndex(const char *name);

} // namespace forest
