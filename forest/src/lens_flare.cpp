#include "lens_flare.hpp"

#include "forest_world.hpp"

#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset/mesh.hpp"
#include "componeng/renderer/asset_manager.hpp"

#include <cmath>

namespace forest {

std::unique_ptr<componeng::renderer::Mesh> buildFlareQuad() {
  auto mesh = std::make_unique<componeng::renderer::Mesh>();
  const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
  for (const auto &c : corners) {
    float v[8] = {c[0], c[1], 0.0f, 0.0f, 0.0f, 1.0f, c[0] * 0.5f + 0.5f, c[1] * 0.5f + 0.5f};
    mesh->m_vertices.insert(mesh->m_vertices.end(), v, v + 8);
  }
  mesh->m_indices = {0, 1, 2, 0, 2, 3};
  return mesh;
}

void LensFlareSystem::onCreate(const componeng::ecs::SystemState &state) {
  auto &assets = state.world->getResource<componeng::renderer::AssetManager>();
  m_material = assets.getMaterialID("lens_flare");
}

void LensFlareSystem::onUpdate(const componeng::ecs::SystemState &state) {
  using namespace componeng;
  ecs::World &world = *state.world;

  const core::Vec3 eye = world
                             .getComponent<core::TransformComponent>(
                                 world.getResource<camera::MainCamera>().entity)
                             .position;
  const float target = world.getResource<ForestWorld>().sunVisibility(eye);

  if (m_first) {
    m_visibility = target;
    m_first = false;
  } else {
    // Quick enough to follow walking past a trunk, slow enough that leaves
    // flickering across the sun do not strobe the flare.
    m_visibility += (target - m_visibility) * (1.0f - std::exp(-7.0f * state.deltaTime));
  }

  world.getResource<renderer::AssetManager>()
      .getMaterial(m_material)
      .setUniform("sunVisible", m_visibility);
}

} // namespace forest
