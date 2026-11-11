#include "water.hpp"

#include "forest_config.hpp"

#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset/mesh.hpp"
#include "componeng/renderer/component/light_component.hpp"
#include "componeng/renderer/component/skybox_component.hpp"
#include "componeng/resources/fog.hpp"

#include <algorithm>
#include <cmath>

namespace forest {

using namespace componeng;

namespace {

// Underwater look. The fog is exp(-(d * density)^2), so 0.18 is nearly opaque
// by 8 m: the player sees little beyond arm's reach, and everything fades to
// this colour (linear RGB), which the sky shows too.
constexpr core::Vec3 kUnderwaterFog(0.003f, 0.020f, 0.050f);
constexpr float kUnderwaterDensity = 0.18f;
// The sun only barely reaches down here, and what does is blue (this also stops
// the fog's warm glow towards the sun).
constexpr core::Vec3 kUnderwaterSun(0.04f, 0.12f, 0.26f);
// Metres below the surface over which the effect fades in, so the camera
// bobbing through the waves does not flicker.
constexpr float kSurfaceBlend = 0.3f;

// The swells, as in water.vert: direction (deg), wavelength (m), amplitude (m), phase.
constexpr float kSwells[3][4] = {{20.0f, 90.0f, 0.45f, 0.0f},
                                 {75.0f, 58.0f, 0.25f, 1.7f},
                                 {-35.0f, 44.0f, 0.14f, 3.1f}};
constexpr float kGravity = 9.81f;

core::Vec3 lerp(const core::Vec3 &a, const core::Vec3 &b, float t) {
  return a + (b - a) * t;
}

} // namespace

float waterSurfaceHeight(float x, float z, float t) {
  float h = cfg::kWaterLevel;
  for (const auto &w : kSwells) {
    float a = core::radians(w[0]);
    float k = 6.2831853f / w[1];
    float omega = std::sqrt(kGravity * k);
    h += w[2] * std::sin(k * (std::cos(a) * x + std::sin(a) * z) - omega * t + w[3]);
  }
  return h;
}

float depthBelowSurface(const core::Vec3 &p, float t) {
  return waterSurfaceHeight(p.x, p.z, t) - p.y;
}

void UnderwaterSystem::onUpdate(const ecs::SystemState &state) {
  ecs::World &world = *state.world;
  auto &fog = world.getResource<resources::Fog>();

  if (!m_captured) {
    m_above.fogColor = fog.color;
    m_above.fogDensity = fog.density;
    world.query().read<renderer::SkyboxComponent>().each(
        [&](const renderer::SkyboxComponent &s) { m_above.skyColor = s.color; });
    world.query().read<renderer::DirectionalLightComponent>().each(
        [&](const renderer::DirectionalLightComponent &l) { m_above.sunColor = l.color; });
    m_captured = true;
  }

  const core::Vec3 eye =
      world.getComponent<core::TransformComponent>(world.getResource<camera::MainCamera>().entity)
          .position;
  const float u =
      std::clamp(depthBelowSurface(eye, float(world.time)) / kSurfaceBlend, 0.0f, 1.0f);
  const float k = u * u * (3.0f - 2.0f * u);

  core::Vec3 above(m_above.fogColor.x, m_above.fogColor.y, m_above.fogColor.z);
  core::Vec3 c = lerp(above, kUnderwaterFog, k);
  fog.color = core::Vec4(c.x, c.y, c.z, m_above.fogColor.w);
  fog.density = m_above.fogDensity + (kUnderwaterDensity - m_above.fogDensity) * k;

  world.query().write<renderer::SkyboxComponent>().each([&](renderer::SkyboxComponent &s) {
    s.color = lerp(m_above.skyColor, kUnderwaterFog, k);
  });
  world.query().write<renderer::DirectionalLightComponent>().each(
      [&](renderer::DirectionalLightComponent &l) {
        l.color = lerp(m_above.sunColor, kUnderwaterSun, k);
      });
}

std::unique_ptr<componeng::renderer::Mesh> buildWaterPlane() {
  const int n = cfg::kWaterCells;
  const float half = 0.5f * cfg::kWaterSize;
  const float cell = cfg::kWaterSize / float(n);

  auto mesh = std::make_unique<componeng::renderer::Mesh>();
  mesh->m_vertices.reserve(size_t(n + 1) * (n + 1) * 8);
  mesh->m_indices.reserve(size_t(n) * n * 6);
  for (int j = 0; j <= n; ++j) {
    for (int i = 0; i <= n; ++i) {
      float x = -half + float(i) * cell, z = -half + float(j) * cell;
      float v[8] = {x, 0.0f, z, 0.0f, 1.0f, 0.0f, x, z};
      mesh->m_vertices.insert(mesh->m_vertices.end(), v, v + 8);
    }
  }
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      unsigned a = unsigned(j * (n + 1) + i), b = a + 1;
      unsigned c = a + unsigned(n + 1), d = c + 1;
      // Counter-clockwise seen from above, like the terrain.
      unsigned tri[6] = {a, c, b, b, c, d};
      mesh->m_indices.insert(mesh->m_indices.end(), tri, tri + 6);
    }
  }
  return mesh;
}

} // namespace forest
