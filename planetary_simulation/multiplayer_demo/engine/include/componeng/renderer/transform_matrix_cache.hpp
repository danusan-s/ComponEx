#pragma once

#include "componeng/core/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/archetype/component_column.hpp"
#include "componeng/ecs/entity.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace componeng::ecs {
class World;
} // namespace componeng::ecs

namespace componeng::renderer {

/**
 * @brief Model matrices of every Transform+Mesh entity, cached per archetype.
 *
 * Rows are stored in the same order as the archetype's Transform column, so
 * an entity's matrix is `models(archetypeId)[row]`. Each archetype remembers
 * the version of its Transform column that its matrices were built from:
 *
 *  - version unchanged: nobody has written the column, or added or removed a
 *    row, since. Every matrix is still valid and refresh() returns after one
 *    integer compare, however many entities the archetype holds.
 *  - version changed: something wrote to the column. The version is per
 *    column, not per entity, so it cannot say which rows moved; refresh()
 *    compares each row's position, rotation and scale (nine floats) with what
 *    the matrix was built from and rebuilds only the rows that differ. A
 *    changed version therefore costs no more than the per-entity cache this
 *    replaces, and a stale matrix is impossible even after rows are shuffled
 *    by a swap-remove.
 *
 * The skip relies on the ECS contract that taking write access to a column
 * (a write<>() query, getComponent<T>(), addComponent, ...) bumps its version
 * *before* the data is touched. Holding a `TransformComponent &` across
 * frames and writing through it later, without taking write access again,
 * bypasses that and the cache will not notice.
 *
 * Threading: refresh() is serial. models() only reads, so it is safe from
 * parallel query bodies provided every refresh() has already happened.
 */
class TransformMatrixCache {
public:
  /**
   * Brings one archetype's matrices up to date. `chunk` is a QueryChunk that
   * declared TransformComponent (read or write) and covers the whole
   * archetype, as eachChunk() provides.
   */
  template <typename Chunk> void refresh(const Chunk &chunk) {
    const std::size_t n = chunk.size();
    const ecs::ColumnVersion version =
        chunk.template version<core::TransformComponent>();
    Slot &slot = slotFor(chunk.archetypeId());

    // Row count is implied by the version (adding or removing a row bumps it);
    // it is checked as well because it costs nothing.
    if (slot.valid && slot.version == version && slot.models.size() == n) {
      return;
    }

    const core::TransformComponent *transforms =
        chunk.template data<core::TransformComponent>();
    const std::size_t known = slot.models.size();
    slot.models.resize(n);
    slot.sources.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      if (i < known && sameTransform(slot.sources[i], transforms[i])) {
        continue;
      }
      slot.sources[i] = transforms[i];
      slot.models[i] = transforms[i].toMatrix();
    }
    slot.version = version;
    slot.valid = true;
  }

  /**
   * The archetype's matrices, indexed by row.
   * @throws std::runtime_error if the archetype was never refreshed, which
   * means the caller's query reaches archetypes refresh() did not.
   */
  const core::Mat4 *models(ecs::ArchetypeID archetype) const {
    if (archetype >= m_slots.size() || !m_slots[archetype].valid) {
      throw std::runtime_error(
          "TransformMatrixCache: archetype was not refreshed");
    }
    return m_slots[archetype].models.data();
  }

  void clear() {
    m_slots.clear();
  }

private:
  struct Slot {
    std::vector<core::TransformComponent> sources; // what each matrix is from
    std::vector<core::Mat4> models;
    ecs::ColumnVersion version = 0;
    bool valid = false;
  };

  Slot &slotFor(ecs::ArchetypeID archetype) {
    if (archetype >= m_slots.size()) {
      m_slots.resize(static_cast<std::size_t>(archetype) + 1);
    }
    return m_slots[archetype];
  }

  static bool sameTransform(const core::TransformComponent &a,
                            const core::TransformComponent &b) {
    return a.position.x == b.position.x && a.position.y == b.position.y &&
           a.position.z == b.position.z && a.rotation.x == b.rotation.x &&
           a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
           a.scale.x == b.scale.x && a.scale.y == b.scale.y &&
           a.scale.z == b.scale.z;
  }

  std::vector<Slot> m_slots;
};

/**
 * @brief The world's shared TransformMatrixCache, refreshed for every
 * archetype that holds both a Transform and a Mesh.
 *
 * Culling and batching both call this at the start of their update, so
 * neither depends on which runs first; the second call finds every archetype
 * unchanged and costs one version compare per archetype. The cache is created
 * as a World resource on first use.
 */
TransformMatrixCache &refreshTransformMatrices(ecs::World &world);

} // namespace componeng::renderer
