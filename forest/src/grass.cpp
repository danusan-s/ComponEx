#include "grass.hpp"

#include "componeng/core/types.hpp"
#include "componeng/renderer/asset/mesh.hpp"

#include <cmath>
#include <random>

namespace forest {

using componeng::core::Vec3;

std::unique_ptr<componeng::renderer::Mesh> buildGrassTuft() {
  constexpr int kBlades = 12;
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
    float ang = uni() * kTau, rad = std::sqrt(uni()) * 0.22f;
    Vec3 root(std::cos(ang) * rad, 0.0f, std::sin(ang) * rad);
    float height = 0.5f + uni() * 0.55f;
    float width = 0.030f + uni() * 0.018f;

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

} // namespace forest
