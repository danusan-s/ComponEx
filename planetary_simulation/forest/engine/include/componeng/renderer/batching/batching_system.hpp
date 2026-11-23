#pragma once

#include "componeng/ecs/system.hpp"
#include "componeng/renderer/batching/draw_key.hpp"
#include "componeng/renderer/batching/instance_data.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace componeng::renderer {

/**
 * @brief Presentation-phase system that turns every visible entity with a
 * Transform, MeshComponent and MaterialComponent into instance data.
 *
 * One pass over the query: each entity's instance data is written by its
 * material directly into the flat buffer of its (mesh, material) batch in the
 * RenderQueue, then the queue is sorted for the RenderSystem to draw.
 * Entities with a ProceduralInstancesComponent instead become one data-less
 * batch each.
 */
class BatchingSystem final : public ecs::ISystem {
public:
  void onCreate(const ecs::SystemState &state) override;
  void onUpdate(const ecs::SystemState &state) override;
  void onDestroy(const ecs::SystemState &state) override;

private:
  // (mesh, material) -> index of its batch in this frame's RenderQueue.
  std::unordered_map<DrawKey, uint32_t, DrawKeyHash> m_batchIndex;
  // Materials already reported for declaring instance attributes on a
  // procedural batch, so the error is logged once, not every frame.
  std::unordered_set<core::HandleID> m_reportedMaterials;
};

} // namespace componeng::renderer
