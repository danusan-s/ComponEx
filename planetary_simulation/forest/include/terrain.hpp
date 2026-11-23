#pragma once

#include "componeng/core/types.hpp"

#include <memory>
#include <vector>

namespace componeng::renderer {
class Mesh;
}

namespace forest {

// Heightfield terrain centred on the origin. Lives in the ForestWorld resource
// rather than a component: components are memcpy-relocated by the ECS, which
// rules out owning a std::vector.
//
// Besides heights it stores lighting baked at start-up (see bakeLighting):
// sun and sky visibility per vertex, plus a finer RGBA8 "grass mask" (density,
// sun, sky) that the grass shader samples.
class Terrain {
public:
  void generate();

  float half() const { return m_half; }
  int stride() const { return m_stride; }
  float cell() const { return m_cell; }

  // Height at a world position. Uses the same triangulation as the mesh, so
  // anything standing on it (the player, grass) sits exactly on the surface.
  float heightAt(float x, float z) const;
  componeng::core::Vec3 normalAt(float x, float z) const;

  float vertexHeight(int i, int j) const { return m_height[index(i, j)]; }
  // Row-major (stride x stride), row = z; uploaded as the grass shader's height map.
  const std::vector<float> &heights() const { return m_height; }
  int index(int i, int j) const { return j * m_stride + i; }

  // Per-vertex baked layers, sampled bilinearly.
  std::vector<float> sunVis, skyVis;

  // kMaskCells x kMaskCells RGBA8 over the world: R = grass density,
  // G = sun visibility, B = sky visibility. Texel (i, j) is centred at
  // world (-half + (i + 0.5) * size / kMaskCells, ...).
  std::vector<unsigned char> grassMask;
  float sample(const std::vector<float> &layer, float x, float z) const;

  // Mesh for terrain chunk (cx, cz); the vertex UV channel carries
  // (sunVis, skyVis).
  std::unique_ptr<componeng::renderer::Mesh> buildChunk(int cx, int cz) const;

private:
  float m_half = 0.0f;
  float m_cell = 0.0f;
  int m_stride = 0;
  std::vector<float> m_height;
};

} // namespace forest
