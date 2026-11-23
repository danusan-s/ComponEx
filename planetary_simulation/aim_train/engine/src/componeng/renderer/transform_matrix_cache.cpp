#include "componeng/renderer/transform_matrix_cache.hpp"

#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/component/mesh_component.hpp"

namespace componeng::renderer {

TransformMatrixCache &refreshTransformMatrices(ecs::World &world) {
  if (!world.hasResource<TransformMatrixCache>()) {
    world.setResource(TransformMatrixCache{});
  }
  TransformMatrixCache &cache = world.getResource<TransformMatrixCache>();

  // Both are only inspected, so this never marks a column written.
  world.query().read<core::TransformComponent, MeshComponent>().eachChunk(
      [&cache](const auto &chunk) { cache.refresh(chunk); });
  return cache;
}

} // namespace componeng::renderer
