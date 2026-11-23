#include "componeng/renderer/culling/culling_system.hpp"
#include "componeng/renderer/culling/bounds.hpp"
#include "componeng/renderer/culling/frustum.hpp"

#include "componeng/camera/camera_component.hpp"
#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/renderer/component/procedural_instances_component.hpp"
#include "componeng/renderer/transform_matrix_cache.hpp"

namespace componeng::renderer {

void CullingSystem::onCreate(const ecs::SystemState &state) {
  // The queries below name this component; registering is idempotent.
  state.world->registerComponent<ProceduralInstancesComponent>();
}

void CullingSystem::onUpdate(const ecs::SystemState &state) {
  ecs::EntityID mainCameraID =
      state.world->getResource<camera::MainCamera>().entity;

  core::Mat4 &viewProj =
      state.world->getComponent<camera::CameraComponent>(mainCameraID)
          .viewProjectionMatrix;

  Frustum frustum(viewProj);
  const AssetManager &assets = state.world->getResource<AssetManager>();

  // The result goes into `culled`, leaving the application's `visible` flag
  // alone. BatchingSystem resolves names to ids on an entity's first frame;
  // until then there is nothing to measure, so it is drawn.
  // Model matrices are cached per archetype and rebuilt only when the
  // Transform column's version moved, so a static scene costs one version
  // compare per archetype here instead of a toMatrix() per entity per frame.
  // Refreshed serially up front; the parallel body below only reads.
  const TransformMatrixCache &matrices = refreshTransformMatrices(*state.world);

  auto test = [&](renderer::MeshComponent &m,
                  const renderer::MaterialComponent &mat,
                  const core::Mat4 &model) {
    if (m.meshID == 0 || mat.materialID == 0) {
      m.culled = false;
      return;
    }

    const Mesh &mesh = assets.getMesh(m.meshID);
    const float padding = assets.getMaterial(mat.materialID).getBoundsPadding();
    const Aabb world = transformAabb(mesh.bounds(), model, padding);
    m.culled = !frustum.isBoxInFrustum(world.min, world.max);
  };

  // Transform stays in these queries though the body only needs the cached
  // matrix: it guarantees every archetype matched is one refreshTransform-
  // Matrices() covered (it refreshes exactly Transform + Mesh archetypes).
  //
  // Every renderable entity is tested against the frustum using its mesh's own
  // bounds under its transform, so culling works with or without a physics
  // collider.
  state.world->query()
      .read<core::TransformComponent>()
      .write<renderer::MeshComponent>()
      .read<renderer::MaterialComponent>()
      .exclude<ProceduralInstancesComponent>()
      .eachChunkParallel(state.world->threadPool(), [&](const auto &chunk) {
        const core::Mat4 *models = matrices.models(chunk.archetypeId());
        renderer::MeshComponent *meshes =
            chunk.template data<renderer::MeshComponent>();
        const renderer::MaterialComponent *materials =
            chunk.template data<renderer::MaterialComponent>();
        for (std::size_t row = chunk.begin(); row < chunk.end(); ++row) {
          test(meshes[row], materials[row], models[row]);
        }
      });

  // Procedural entities place their instances in the shader, so the mesh's
  // bounds only mean something if they opted in.
  state.world->query()
      .read<core::TransformComponent>()
      .write<renderer::MeshComponent>()
      .read<renderer::MaterialComponent, ProceduralInstancesComponent>()
      .eachChunk([&](const auto &chunk) {
        const core::Mat4 *models = matrices.models(chunk.archetypeId());
        renderer::MeshComponent *meshes =
            chunk.template data<renderer::MeshComponent>();
        const renderer::MaterialComponent *materials =
            chunk.template data<renderer::MaterialComponent>();
        const ProceduralInstancesComponent *procedurals =
            chunk.template data<ProceduralInstancesComponent>();
        for (std::size_t row = chunk.begin(); row < chunk.end(); ++row) {
          if (procedurals[row].cull) {
            test(meshes[row], materials[row], models[row]);
          } else {
            meshes[row].culled = false;
          }
        }
      });
}

} // namespace componeng::renderer
