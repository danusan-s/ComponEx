#include "billboard.hpp"

#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset/mesh.hpp"
#include "componeng/renderer/component/mesh_component.hpp"

#include <cstdlib>

namespace forest {

using namespace componeng;

float billboardNearDistance() {
  static const float value = [] {
    const char *env = std::getenv("FOREST_BILLBOARD_NEAR");
    return env ? float(std::atof(env)) : 60.0f;
  }();
  return value;
}

std::unique_ptr<renderer::Mesh> buildBillboardQuad(float halfWidth, float bottom, float top) {
  auto mesh = std::make_unique<renderer::Mesh>();
  const float corners[4][4] = {{-halfWidth, bottom, 0.0f, 1.0f},
                               {halfWidth, bottom, 1.0f, 1.0f},
                               {halfWidth, top, 1.0f, 0.0f},
                               {-halfWidth, top, 0.0f, 0.0f}};
  for (const auto &c : corners) {
    float v[8] = {c[0], c[1], 0.0f, 0.0f, 0.0f, 1.0f, c[2], c[3]};
    mesh->m_vertices.insert(mesh->m_vertices.end(), v, v + 8);
  }
  mesh->m_indices = {0, 1, 2, 0, 2, 3};
  return mesh;
}

void BillboardLodSystem::onUpdate(const ecs::SystemState &state) {
  ecs::World &world = *state.world;
  const core::Vec3 cam = world
                             .getComponent<core::TransformComponent>(
                                 world.getResource<camera::MainCamera>().entity)
                             .position;

  world.query<core::TransformComponent, renderer::MeshComponent, BillboardLodComponent>()
      .eachParallel(world.threadPool(),
                    [cam](core::TransformComponent &transform, renderer::MeshComponent &mesh,
                          BillboardLodComponent &lod) {
                      // Same distance the engine's LodSystem uses for the 3D levels.
                      const float dx = transform.position.x - cam.x;
                      const float dy = transform.position.y - cam.y;
                      const float dz = transform.position.z - cam.z;
                      mesh.visible =
                          dx * dx + dy * dy + dz * dz >= lod.nearDistance * lod.nearDistance;
                    });
}

} // namespace forest
