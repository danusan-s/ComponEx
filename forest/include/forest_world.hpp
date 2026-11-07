#pragma once

#include "terrain.hpp"

#include "componeng/core/types.hpp"

#include <algorithm>
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

// A tree's crown as an ellipsoid, for shadow and occlusion tests.
struct Crown {
  componeng::core::Vec3 c;
  float rh, rv;  // horizontal / vertical radius
  float density; // optical depth per metre of chord
};

// Spatial hash of crowns: for the lighting bake and the runtime sun query.
class CrownGrid {
public:
  static constexpr float kCell = 16.0f;

  void build(const std::vector<Crown> &crowns, float half) {
    m_half = half;
    m_n = int(std::ceil(2.0f * half / kCell)) + 1;
    m_cells.assign(size_t(m_n) * m_n, {});
    for (size_t i = 0; i < crowns.size(); ++i) {
      m_cells[size_t(cellOf(crowns[i].c.z)) * m_n + cellOf(crowns[i].c.x)].push_back(int(i));
    }
  }

  template <class F> void near(float x, float z, int cells, F &&fn) const {
    int ci = cellOf(x), cj = cellOf(z);
    for (int j = std::max(cj - cells, 0); j <= std::min(cj + cells, m_n - 1); ++j)
      for (int i = std::max(ci - cells, 0); i <= std::min(ci + cells, m_n - 1); ++i)
        for (int id : m_cells[size_t(j) * m_n + i]) fn(id);
  }

private:
  int cellOf(float v) const {
    int c = int(std::floor((v + m_half) / kCell));
    return std::clamp(c, 0, m_n - 1);
  }
  float m_half = 0.0f;
  int m_n = 0;
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

  // Every tree's crown ellipsoid and a grid over them, kept after the bake.
  // crowns[i] belongs to trees[i].
  std::vector<Crown> crowns;
  CrownGrid crownGrid;

  void build();

  // Fraction of the sun's light that reaches `eye` along the ray to the sun:
  // 0 behind terrain or a trunk, attenuated by tree crowns. For things that
  // must know whether the sun can be seen (the lens flare); the engine offers
  // no depth-buffer read-back or occlusion query to ask the GPU instead.
  float sunVisibility(const componeng::core::Vec3 &eye) const;
};

// Index of a named model in kPropModels, or -1.
int propIndex(const char *name);

} // namespace forest
