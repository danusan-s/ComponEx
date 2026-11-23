#include "componeng/renderer/batching/batching_system.hpp"

#include "componeng/core/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/entity.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset/material.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/batching/render_queue.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/renderer/component/procedural_instances_component.hpp"
#include "componeng/renderer/transform_matrix_cache.hpp"
#include "componeng/utils/logger.hpp"

namespace componeng::renderer {

void BatchingSystem::onCreate(const ecs::SystemState &state) {
  state.world->setResource(RenderQueue());
  // Both queries below name this component; registering is idempotent.
  state.world->registerComponent<ProceduralInstancesComponent>();
}

void BatchingSystem::onUpdate(const ecs::SystemState &state) {
  const AssetManager &assetManager = state.world->getResource<AssetManager>();
  RenderQueue &renderQueue = state.world->getResource<RenderQueue>();

  renderQueue.clear();
  m_batchIndex.clear();

  // Model matrices come from the shared cache: an archetype whose Transform
  // column has not been written since last frame costs one version compare.
  const TransformMatrixCache &matrices = refreshTransformMatrices(*state.world);

  // Entities of one archetype tend to arrive in runs sharing a mesh and
  // material (a forest of identical trees), so remember the last batch and
  // skip the hash lookup while the run lasts.
  constexpr uint32_t kNoBatch = UINT32_MAX;
  DrawKey lastKey;
  uint32_t lastBatch = kNoBatch;
  const IMaterial *lastMaterial = nullptr;

  // Resolves an entity's names to ids on its first frame, and returns its
  // material.
  auto resolve =
      [&assetManager](renderer::MeshComponent &m,
                      renderer::MaterialComponent &mat) -> IMaterial & {
    if (mat.materialID == 0) {
      mat.materialID = assetManager.getMaterialID(
          mat.materialName.empty() ? core::Name("default_diffuse")
                                   : mat.materialName);
    }
    IMaterial &material = assetManager.getMaterial(mat.materialID);
    if (m.meshID == 0) {
      m.meshID = assetManager.getMeshID(m.meshName.empty() ? core::Name("cube")
                                                           : m.meshName);
    }
    return material;
  };

  state.world->query()
      .read<core::TransformComponent>()
      .write<renderer::MeshComponent, renderer::MaterialComponent>()
      .exclude<ProceduralInstancesComponent>()
      .eachChunk([&](const auto &chunk) {
        const core::Mat4 *models = matrices.models(chunk.archetypeId());
        const core::TransformComponent *transforms =
            chunk.template data<core::TransformComponent>();
        renderer::MeshComponent *meshes =
            chunk.template data<renderer::MeshComponent>();
        renderer::MaterialComponent *materials =
            chunk.template data<renderer::MaterialComponent>();

        for (std::size_t row = chunk.begin(); row < chunk.end(); ++row) {
          renderer::MeshComponent &m = meshes[row];
          renderer::MaterialComponent &mat = materials[row];
          IMaterial &material = resolve(m, mat);

          if (!m.visible || m.culled) {
            continue;
          }

          // Writes one instance of `meshID` into its batch. `fade` is the
          // value for the shader's `instanceFade` (0: none).
          auto emit = [&](core::HandleID meshID, float fade) {
            const DrawKey key{meshID, mat.materialID};
            if (lastBatch == kNoBatch || !(key == lastKey)) {
              auto [it, inserted] = m_batchIndex.try_emplace(key, kNoBatch);
              if (inserted) {
                it->second = renderQueue.openBatch(
                    key.meshID, key.materialID, material.getShaderID(),
                    material.getTextureID(), material.isTransparent());
              }
              lastKey = key;
              lastBatch = it->second;
              lastMaterial = &material;
            }

            RenderBatch &batch = renderQueue.batch(lastBatch);
            const api::VertexLayout &layout = lastMaterial->getVertexLayout();
            const std::size_t strideFloats = layout.stride / sizeof(float);

            // resize() zero-fills the new instance, so any attribute the
            // material does not write reads as 0.
            const std::size_t base = batch.instanceData.size();
            batch.instanceData.resize(base + strideFloats);

            InstanceWriter writer(layout, batch.instanceData.data() + base);
            InstanceContext ctx(*state.world, chunk.entity(row),
                                transforms[row], models[row]);
            lastMaterial->writeInstanceData(ctx, writer);
            if (fade != 0.0f) {
              lastMaterial->writeLodFade(writer, fade);
              batch.fading = true;
            }
            ++batch.instanceCount;
          };

          if (m.fade == 0.0f) {
            emit(m.meshID, 0.0f);
          } else if (!material.supportsLodFade()) {
            // Its shader cannot dither: keep the hard switch, which shows the
            // incoming mesh and nothing once the entity is fading to nothing.
            if (m.fade < 0.0f) {
              emit(m.meshID, 0.0f);
            }
          } else {
            emit(m.meshID, m.fade);
            if (m.fadeMeshID != 0) {
              // The other mesh takes the complementary pattern: where this
              // one is dithering in at progress t, that one dithers out at t.
              emit(m.fadeMeshID, m.fade > 0.0f ? m.fade - 1.0f : m.fade + 1.0f);
            }
          }
        }
      });

  // Procedural entities: one batch each, no instance data. The shader places
  // every instance from gl_InstanceID.
  state.world->query()
      .read<core::TransformComponent>()
      .write<renderer::MeshComponent, renderer::MaterialComponent>()
      .read<ProceduralInstancesComponent>()
      .each([&](const core::TransformComponent &, renderer::MeshComponent &m,
                renderer::MaterialComponent &mat,
                const ProceduralInstancesComponent &procedural) {
        IMaterial &material = resolve(m, mat);

        if (!m.visible || m.culled || procedural.count <= 0) {
          return;
        }

        // With no instance buffer, a per-instance attribute would read zero.
        if (!material.getVertexLayout().attributes.empty() &&
            m_reportedMaterials.insert(mat.materialID).second) {
          LOG_ERROR("Material '%s' has per-instance attributes but is drawn "
                    "with ProceduralInstancesComponent, which supplies none; "
                    "they will read as zero",
                    mat.materialName.c_str());
        }

        const uint32_t index = renderQueue.openBatch(
            m.meshID, mat.materialID, material.getShaderID(),
            material.getTextureID(), material.isTransparent());
        RenderBatch &batch = renderQueue.batch(index);
        batch.procedural = true;
        batch.instanceCount = static_cast<uint32_t>(procedural.count);
      });

  renderQueue.sort();
}

void BatchingSystem::onDestroy(const ecs::SystemState &) {
  m_batchIndex.clear();
  m_reportedMaterials.clear();
}

} // namespace componeng::renderer
