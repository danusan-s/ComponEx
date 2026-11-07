#include "forest_world.hpp"

#include "forest_config.hpp"
#include "noise.hpp"
#include "species_data.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <random>
#include <thread>

namespace forest {

using componeng::core::Vec3;

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;
constexpr int kSpeciesCount = int(std::size(kTreeSpecies));
constexpr int kPropCount = int(std::size(kPropModels));

bool isConifer(int species) { return species >= 2; }

Crown crownOf(const TreePlacement &t) {
  const TreeSpecies &s = kTreeSpecies[t.species];
  float cy = std::cos(t.yaw * kDeg), sy = std::sin(t.yaw * kDeg);
  float lx = s.crownX * t.scale, lz = s.crownZ * t.scale;
  Crown c;
  c.c = Vec3(t.pos.x + cy * lx + sy * lz, t.pos.y + s.crownY * t.scale,
             t.pos.z - sy * lx + cy * lz);
  c.rh = s.crownRadius * t.scale * 1.05f;
  c.rv = std::max(0.5f * (s.crownTop - s.crownBottom) * t.scale, 1.0f);
  c.density = isConifer(t.species) ? 0.26f : 0.19f;
  return c;
}

// Length of the part of a ray (origin o, unit direction d, t >= 0) inside the
// ellipsoid.
float chord(const Vec3 &o, const Vec3 &d, const Crown &c) {
  float px = (o.x - c.c.x) / c.rh, py = (o.y - c.c.y) / c.rv, pz = (o.z - c.c.z) / c.rh;
  float dx = d.x / c.rh, dy = d.y / c.rv, dz = d.z / c.rh;
  float a = dx * dx + dy * dy + dz * dz;
  float b = px * dx + py * dy + pz * dz;
  float k = px * px + py * py + pz * pz - 1.0f;
  float disc = b * b - a * k;
  if (disc <= 0.0f) return 0.0f;
  float sq = std::sqrt(disc);
  float t1 = (-b - sq) / a, t2 = (-b + sq) / a;
  if (t2 <= 0.0f) return 0.0f;
  return t2 - std::max(t1, 0.0f);
}

template <class F> void parallelFor(int n, F &&fn) {
  unsigned workers = std::max(1u, std::thread::hardware_concurrency());
  std::atomic<int> next{0};
  std::vector<std::thread> pool;
  for (unsigned w = 0; w < workers; ++w) {
    pool.emplace_back([&] {
      for (;;) {
        int i = next.fetch_add(1);
        if (i >= n) break;
        fn(i);
      }
    });
  }
  for (auto &t : pool) t.join();
}

float forestDensity(float x, float z) {
  using namespace noise;
  float r = std::sqrt(x * x + z * z);
  float meadow = smoothstep(24.0f, 70.0f, r);
  float groves = smoothstep(0.36f, 0.58f, fbm(x * 0.0105f + 40.0f, z * 0.0105f - 8.0f, 3, 5));
  return meadow * (0.10f + 0.90f * groves);
}

// Lowest ground within `radius` of (x, z). Standing a model on this, then
// sinking it a little, buries the uphill side of its base instead of leaving
// the downhill side hanging in the air on a slope.
float lowestGround(const Terrain &t, float x, float z, float radius) {
  float low = t.heightAt(x, z);
  for (int k = 0; k < 8; ++k) {
    float a = float(k) * 0.785398f;
    low = std::min(low, t.heightAt(x + std::cos(a) * radius, z + std::sin(a) * radius));
  }
  return low;
}

struct Rng {
  std::mt19937 gen;
  explicit Rng(unsigned seed) : gen(seed) {}
  float uni() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(gen); }
  float range(float a, float b) { return a + (b - a) * uni(); }
};

void placeTrees(ForestWorld &w, Rng &rng) {
  const Terrain &t = w.terrain;
  const float limit = t.half() - 26.0f;

  constexpr float kGridCell = 12.0f;
  const int gn = int(std::ceil(2.0f * t.half() / kGridCell)) + 1;
  std::vector<std::vector<int>> grid(size_t(gn) * gn);
  auto cellOf = [&](float v) {
    return std::clamp(int(std::floor((v + t.half()) / kGridCell)), 0, gn - 1);
  };

  for (int attempt = 0; attempt < 220000; ++attempt) {
    float x = rng.range(-limit, limit), z = rng.range(-limit, limit);
    if (rng.uni() > forestDensity(x, z)) continue;
    Vec3 n = t.normalAt(x, z);
    if (n.y < 0.80f) continue;

    TreePlacement tp;
    float conifer = noise::smoothstep(0.42f, 0.58f, noise::fbm(x * 0.0085f + 55.0f, z * 0.0085f - 12.0f, 3, 6));
    if (rng.uni() < conifer) tp.species = 2 + int(rng.uni() * 2.999f);
    else tp.species = rng.uni() < 0.5f ? 0 : 1;

    const TreeSpecies &sp = kTreeSpecies[tp.species];
    float u = std::pow(rng.uni(), 0.85f);
    tp.scale = sp.minScale + (sp.maxScale - sp.minScale) * u;
    float footprint = std::max(sp.trunkRadius * tp.scale * 1.8f, 0.6f);
    tp.pos = Vec3(x, lowestGround(t, x, z, footprint) - 0.28f, z);
    tp.yaw = rng.range(0.0f, 360.0f);
    tp.tiltX = rng.range(-1.8f, 1.8f);
    tp.tiltZ = rng.range(-1.8f, 1.8f);
    float v = rng.range(0.88f, 1.12f);
    tp.tint = Vec3(v * rng.range(0.9f, 1.1f), v * rng.range(0.95f, 1.08f), v * rng.range(0.85f, 1.05f));

    // Crowns may overlap, but not sit on top of one another.
    float ra = sp.crownRadius * tp.scale;
    bool blocked = false;
    int ci = cellOf(x), cj = cellOf(z);
    for (int j = std::max(cj - 1, 0); j <= std::min(cj + 1, gn - 1) && !blocked; ++j) {
      for (int i = std::max(ci - 1, 0); i <= std::min(ci + 1, gn - 1) && !blocked; ++i) {
        for (int id : grid[size_t(j) * gn + i]) {
          const TreePlacement &o = w.trees[size_t(id)];
          float rb = kTreeSpecies[o.species].crownRadius * o.scale;
          float dx = o.pos.x - x, dz = o.pos.z - z;
          float need = 0.62f * (ra + rb);
          if (dx * dx + dz * dz < need * need) {
            blocked = true;
            break;
          }
        }
      }
    }
    if (blocked) continue;

    grid[size_t(cj) * gn + ci].push_back(int(w.trees.size()));
    w.trees.push_back(tp);
    w.obstacles.add({x, z, std::max(sp.trunkRadius * tp.scale, 0.2f), 100.0f, false, tp.pos.y});
  }
}

void addPropObstacles(ForestWorld &w, const PropPlacement &p) {
  const PropModel &m = kPropModels[p.model];
  const char *name = m.name;
  float cy = std::cos(p.yaw * kDeg), sy = std::sin(p.yaw * kDeg);
  if (std::strcmp(name, "stump") == 0) {
    w.obstacles.add({p.pos.x, p.pos.z, 0.5f * 0.5f * (m.extentX + m.extentZ) * p.scale * 0.85f,
                     m.height * p.scale, true, p.pos.y});
  } else if (std::strcmp(name, "log") == 0) {
    float length = m.extentX * p.scale, r = 0.5f * m.extentZ * p.scale * 0.92f;
    int count = std::max(1, int(std::ceil((length - 2.0f * r) / (r * 1.3f))) + 1);
    for (int k = 0; k < count; ++k) {
      float t = count == 1 ? 0.0f : (-0.5f * length + r) + (length - 2.0f * r) * float(k) / float(count - 1);
      w.obstacles.add({p.pos.x + cy * t, p.pos.z - sy * t, r, m.height * p.scale * 0.95f, false, p.pos.y});
    }
  } else if (std::strncmp(name, "rock", 4) == 0 || std::strcmp(name, "boulder") == 0) {
    w.obstacles.add({p.pos.x, p.pos.z, 0.5f * 0.5f * (m.extentX + m.extentZ) * p.scale * 0.8f,
                     m.height * p.scale * 0.85f, false, p.pos.y});
  }
}

void placeProps(ForestWorld &w, Rng &rng) {
  const Terrain &t = w.terrain;
  const float limit = t.half() - 20.0f;

  std::vector<int> rocks;
  int boulder = propIndex("boulder"), log = propIndex("log"), stump = propIndex("stump");
  for (int i = 0; i < kPropCount; ++i) {
    if (std::strncmp(kPropModels[i].name, "rock", 4) == 0) rocks.push_back(i);
  }

  auto blockedAt = [&](float x, float z, float r) {
    bool hit = false;
    w.obstacles.query(x, z, r + 1.0f, [&](const Obstacle &o) {
      float dx = o.x - x, dz = o.z - z, rr = o.r + r;
      if (dx * dx + dz * dz < rr * rr) hit = true;
    });
    return hit;
  };
  auto place = [&](int model, float x, float z, float scale, float sink) {
    PropPlacement p;
    p.model = model;
    p.scale = scale;
    float footprint = 0.25f * (kPropModels[model].extentX + kPropModels[model].extentZ) * scale;
    p.pos = Vec3(x, lowestGround(t, x, z, std::min(footprint, 1.2f)) - sink * scale, z);
    p.yaw = rng.range(0.0f, 360.0f);
    p.tiltX = p.tiltZ = 0.0f;
    float v = rng.range(0.85f, 1.12f);
    p.tint = Vec3(v * rng.range(0.92f, 1.08f), v, v * rng.range(0.88f, 1.05f));
    w.props.push_back(p);
    return int(w.props.size()) - 1;
  };

  // Rocks: more of them on slopes.
  for (int c = 0; c < 2600; ++c) {
    float x = rng.range(-limit, limit), z = rng.range(-limit, limit);
    float ny = t.normalAt(x, z).y;
    if (ny < 0.5f) continue;
    if (rng.uni() > 0.035f + 1.6f * (1.0f - ny)) continue;
    if (blockedAt(x, z, 1.0f)) continue;
    bool big = rng.uni() < 0.3f;
    int m = big ? boulder : rocks[size_t(rng.uni() * float(rocks.size()) * 0.9999f)];
    float s = big ? rng.range(0.9f, 2.2f) : rng.range(0.3f, 0.85f);
    int id = place(m, x, z, s, 0.22f * kPropModels[m].height);
    addPropObstacles(w, w.props[size_t(id)]);
  }

  // Fallen logs and stumps, in the woods.
  int logs = 0, stumps = 0;
  for (int c = 0; c < 4000 && (logs < 55 || stumps < 90); ++c) {
    float x = rng.range(-limit, limit), z = rng.range(-limit, limit);
    float d = forestDensity(x, z);
    if (d < 0.3f || t.normalAt(x, z).y < 0.9f) continue;
    bool wantLog = rng.uni() < 0.4f;
    int model = wantLog ? log : stump;
    if (wantLog ? logs >= 55 : stumps >= 90) continue;
    float s = rng.range(0.8f, 1.5f);
    if (blockedAt(x, z, wantLog ? 2.2f * s : 0.9f * s)) continue;
    int id = place(model, x, z, s, wantLog ? 0.0f : 0.12f);
    (wantLog ? logs : stumps)++;
    PropPlacement &p = w.props[size_t(id)];
    if (wantLog) {
      // Rest the log on the average of the ground under its ends and middle,
      // so it neither floats on one side nor is buried on the other.
      float cy = std::cos(p.yaw * kDeg), sy = std::sin(p.yaw * kDeg);
      float h = 0.0f;
      for (float k : {-1.0f, 0.0f, 1.0f}) {
        float dx = cy * k * 1.9f * s, dz = -sy * k * 1.9f * s;
        h += t.heightAt(p.pos.x + dx, p.pos.z + dz);
      }
      p.pos.y = h / 3.0f + 0.12f * s;
    }
    addPropObstacles(w, p);
  }
}

// ---------------------------------------------------------------------------
// Lighting bake. The engine has no shadow pass, but the sun never moves, so the
// shadows can be computed once: terrain self-shadowing by marching the
// heightfield towards the sun, and tree shadows by tracing the same ray
// through each tree's crown ellipsoid. Results are stored per terrain vertex
// (and per tree for how much sun reaches its crown).
// ---------------------------------------------------------------------------
float terrainShadow(const Terrain &t, const Vec3 &o, const Vec3 &L) {
  float res = 1.0f;
  float lh = std::sqrt(L.x * L.x + L.z * L.z);
  for (float s = 2.0f; s < 170.0f; s *= 1.18f) {
    float x = o.x + L.x / lh * s, z = o.z + L.z / lh * s;
    if (std::fabs(x) > t.half() || std::fabs(z) > t.half()) break;
    float rayY = o.y + L.y / lh * s;
    float h = t.heightAt(x, z);
    res = std::min(res, std::clamp(6.0f * (rayY - h) / s, 0.0f, 1.0f));
    if (res <= 0.0f) break;
  }
  return res;
}

float canopyShadow(const CrownGrid &grid, const std::vector<Crown> &crowns, const Vec3 &o,
                   const Vec3 &L, int skip) {
  float tau = 0.0f;
  grid.near(o.x, o.z, 3, [&](int id) {
    if (id == skip) return;
    const Crown &c = crowns[size_t(id)];
    tau += chord(o, L, c) * c.density;
  });
  return std::exp(-tau);
}

// Grass density and baked light at the mask's finer resolution. Grass is dense
// everywhere, like the meadow at the spawn: only ground too steep to hold it,
// and the footprint of trunks and rocks, is bare. Doing this per mask texel
// (0.625 m) rather than per terrain vertex (1.25 m) lets a 1 m rock cut a clean
// hole instead of a blur.
void bakeGrassMask(ForestWorld &w) {
  Terrain &t = w.terrain;
  const int res = cfg::kMaskCells;
  const float texel = cfg::kWorldSize / float(res);
  t.grassMask.assign(size_t(res) * res * 4, 0);

  parallelFor(res, [&](int j) {
    for (int i = 0; i < res; ++i) {
      float x = -t.half() + (float(i) + 0.5f) * texel;
      float z = -t.half() + (float(j) + 0.5f) * texel;

      float ny = t.normalAt(x, z).y;
      float d = noise::smoothstep(0.62f, 0.80f, ny);
      d *= 0.88f + 0.12f * noise::fbm(x * 0.06f + 3.0f, z * 0.06f, 3, 9);
      w.obstacles.query(x, z, 3.0f, [&](const Obstacle &o) {
        float dx = o.x - x, dz = o.z - z;
        float dist = std::sqrt(dx * dx + dz * dz) - o.r;
        // Trunks (top >= 99) get a slightly tighter ring than rocks and logs.
        d *= noise::smoothstep(0.0f, o.top >= 99.0f ? 0.45f : 0.5f, dist);
      });

      auto q = [](float v) {
        return static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
      };
      unsigned char *px = &t.grassMask[(size_t(j) * res + i) * 4];
      px[0] = q(d);
      px[1] = q(t.sample(t.sunVis, x, z));
      px[2] = q(t.sample(t.skyVis, x, z));
      px[3] = 255;
    }
  });
}

void bakeLighting(ForestWorld &w) {
  Terrain &t = w.terrain;
  const Vec3 L = w.sunDir;

  // Kept in the world: the runtime sun-visibility query (for the lens flare)
  // traces the same crowns.
  std::vector<Crown> &crowns = w.crowns;
  crowns.clear();
  crowns.reserve(w.trees.size());
  for (const TreePlacement &tp : w.trees) crowns.push_back(crownOf(tp));
  CrownGrid &grid = w.crownGrid;
  grid.build(crowns, t.half());

  // Sun reaching each tree's crown, and each prop, after the other trees.
  parallelFor(int(w.trees.size()), [&](int i) {
    const Crown &c = crowns[size_t(i)];
    Vec3 o(c.c.x, c.c.y + c.rv * 0.4f, c.c.z);
    w.trees[size_t(i)].sunVis =
        std::clamp(canopyShadow(grid, crowns, o, L, i) * terrainShadow(t, o, L), 0.12f, 1.0f);
  });
  parallelFor(int(w.props.size()), [&](int i) {
    PropPlacement &p = w.props[size_t(i)];
    Vec3 o(p.pos.x, p.pos.y + 0.5f, p.pos.z);
    p.sunVis = std::clamp(canopyShadow(grid, crowns, o, L, -1) * terrainShadow(t, o, L), 0.0f, 1.0f);
  });

  const int stride = t.stride();
  parallelFor(stride, [&](int j) {
    for (int i = 0; i < stride; ++i) {
      float x = -t.half() + float(i) * t.cell(), z = -t.half() + float(j) * t.cell();
      float h = t.vertexHeight(i, j);
      Vec3 o(x, h + 0.4f, z);
      size_t idx = size_t(t.index(i, j));

      float sun = terrainShadow(t, o, L) * canopyShadow(grid, crowns, o, L, -1);
      t.sunVis[idx] = std::clamp(sun, 0.0f, 1.0f);

      // Sky visibility: crowns overhead, trunk contact, and concavity.
      float occl = 1.0f;
      float trunkDist = 100.0f;
      grid.near(x, z, 1, [&](int id) {
        const Crown &c = crowns[size_t(id)];
        float dx = c.c.x - x, dz = c.c.z - z;
        float dh = std::sqrt(dx * dx + dz * dz);
        float coverage = 1.0f - noise::smoothstep(0.35f * c.rh, 1.15f * c.rh, dh);
        float above = std::max((c.c.y - c.rv) - h, 0.0f);
        float strength = isConifer(w.trees[size_t(id)].species) ? 0.52f : 0.44f;
        occl *= 1.0f - strength * coverage / (1.0f + above / 9.0f);
        const TreePlacement &tp = w.trees[size_t(id)];
        float tx = tp.pos.x - x, tz = tp.pos.z - z;
        trunkDist = std::min(trunkDist, std::sqrt(tx * tx + tz * tz) - kTreeSpecies[tp.species].trunkRadius * tp.scale);
      });
      occl *= 1.0f - 0.4f * (1.0f - noise::smoothstep(0.0f, 1.6f, trunkDist));

      float avg = 0.0f;
      for (int k = 0; k < 8; ++k) {
        float a = float(k) * 0.785398f;
        avg += t.heightAt(x + std::cos(a) * 4.0f, z + std::sin(a) * 4.0f);
      }
      avg /= 8.0f;
      float concave = std::clamp(1.0f - (avg - h) * 0.07f, 0.55f, 1.0f);
      t.skyVis[idx] = std::clamp(occl * concave, 0.18f, 1.0f);

    }
  });

  bakeGrassMask(w);
}

} // namespace

int propIndex(const char *name) {
  for (int i = 0; i < kPropCount; ++i)
    if (std::strcmp(kPropModels[i].name, name) == 0) return i;
  return -1;
}

float ForestWorld::sunVisibility(const Vec3 &eye) const {
  const Vec3 L = sunDir;

  // Terrain, then the crowns (soft: leaves let some light through).
  float vis = terrainShadow(terrain, eye, L) * canopyShadow(crownGrid, crowns, eye, L, -1);
  if (vis <= 0.0f) return 0.0f;

  // Trunks block outright. Each is a vertical cylinder from the tree's base to
  // its top; test the sun ray against the ones near the eye.
  const float lh2 = L.x * L.x + L.z * L.z;
  if (lh2 > 1e-6f) {
    bool blocked = false;
    crownGrid.near(eye.x, eye.z, 1, [&](int id) {
      if (blocked) return;
      const TreePlacement &tp = trees[size_t(id)];
      const TreeSpecies &sp = kTreeSpecies[tp.species];
      float r = std::max(sp.trunkRadius * tp.scale, 0.2f);
      float dx = eye.x - tp.pos.x, dz = eye.z - tp.pos.z;
      float b = dx * L.x + dz * L.z;
      float c = dx * dx + dz * dz - r * r;
      float disc = b * b - lh2 * c;
      if (disc <= 0.0f) return;
      float sq = std::sqrt(disc);
      float t2 = (-b + sq) / lh2;
      if (t2 <= 0.0f) return;  // the trunk is behind the ray
      float t = std::max((-b - sq) / lh2, 0.0f);
      float y = eye.y + L.y * t;
      if (y >= tp.pos.y && y <= tp.pos.y + sp.height * tp.scale) blocked = true;
    });
    if (blocked) return 0.0f;
  }
  return vis;
}

void ForestWorld::build() {
  sunDir = componeng::core::normalize(Vec3(cfg::kSunX, cfg::kSunY, cfg::kSunZ));
  terrain.generate();
  obstacles.init(terrain.half());
  Rng rng(cfg::kSeed);
  placeTrees(*this, rng);
  placeProps(*this, rng);
  bakeLighting(*this);
  (void)kSpeciesCount;
}

} // namespace forest
