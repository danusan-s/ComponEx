#pragma once

#include "componeng/core/types.hpp"
#include "componeng/renderer/batching/draw_key.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

namespace componeng::renderer {

/**
 * @brief One instanced draw: every entity sharing a (mesh, material).
 *
 * `instanceData` is `instanceCount` instances packed back to back, each
 * `material.getVertexLayout().stride` bytes, ready to upload as-is.
 *
 * A `procedural` batch (ProceduralInstancesComponent) has `instanceCount`
 * instances but no `instanceData`: the shader places them from gl_InstanceID.
 */
struct RenderBatch {
  core::HandleID meshID = 0;
  core::HandleID materialID = 0;
  // Copied off the material when the batch is opened; they are the sort keys
  // and let the renderer notice shader/texture changes between draws.
  core::HandleID shaderID = 0;
  core::HandleID textureID = 0;
  // Copied off the material (IMaterial::isTransparent); drawn after every
  // opaque batch.
  bool transparent = false;

  uint32_t instanceCount = 0;
  bool procedural = false;
  // True if any instance carries a LOD cross-fade value, so the depth pre-pass
  // must run the fragment stage's discard too.
  bool fading = false;
  std::vector<float> instanceData;
};

/**
 * @brief Per-frame list of draws, built by BatchingSystem and consumed by
 * RenderSystem.
 *
 * Batches live in a pool that survives clear(): the instance vectors keep
 * their capacity, so after the first frames a steady scene builds its queue
 * without touching the allocator. Only batches opened since the last clear()
 * exist, so a mesh/material pair that is no longer drawn costs nothing.
 */
class RenderQueue {
public:
  /** Opens a fresh, empty batch and returns its index. Indices (not
   *  references) are the stable handle: opening another batch may grow the
   *  pool. */
  uint32_t openBatch(core::HandleID meshID, core::HandleID materialID,
                     core::HandleID shaderID, core::HandleID textureID,
                     bool transparent = false) {
    if (m_active == m_pool.size()) {
      m_pool.emplace_back();
    }
    RenderBatch &batch = m_pool[m_active];
    batch.meshID = meshID;
    batch.materialID = materialID;
    batch.shaderID = shaderID;
    batch.textureID = textureID;
    batch.transparent = transparent;
    batch.instanceCount = 0;
    batch.procedural = false;
    batch.fading = false;
    batch.instanceData.clear();
    return static_cast<uint32_t>(m_active++);
  }

  RenderBatch &batch(uint32_t index) {
    return m_pool[index];
  }

  /** Orders the open batches opaque first, transparent last, and within each
   *  by shader, then texture, then material, then mesh, so draws sharing GPU
   * state are adjacent and the order is the same every frame. Call once after
   * the last openBatch(). */
  void sort() {
    m_order.resize(m_active);
    for (std::size_t i = 0; i < m_active; ++i) {
      m_order[i] = static_cast<uint32_t>(i);
    }
    std::sort(m_order.begin(), m_order.end(), [this](uint32_t a, uint32_t b) {
      const RenderBatch &x = m_pool[a];
      const RenderBatch &y = m_pool[b];
      return std::tie(x.transparent, x.shaderID, x.textureID, x.materialID,
                      x.meshID) < std::tie(y.transparent, y.shaderID,
                                           y.textureID, y.materialID, y.meshID);
    });
  }

  /** Number of batches to draw. */
  std::size_t size() const {
    return m_order.size();
  }

  /** How many of the first size() batches are opaque, i.e. the opaque draws
   *  are sorted(0 .. opaqueCount()-1) and the transparent ones follow. Valid
   *  after sort(). */
  std::size_t opaqueCount() const {
    return static_cast<std::size_t>(
        std::partition_point(m_order.begin(), m_order.end(),
                             [this](uint32_t i) {
                               return !m_pool[i].transparent;
                             }) -
        m_order.begin());
  }

  /** i-th batch in draw order; valid after sort(). */
  const RenderBatch &sorted(std::size_t i) const {
    return m_pool[m_order[i]];
  }

  /** Forgets this frame's batches but keeps their storage for reuse. */
  void clear() {
    m_active = 0;
    m_order.clear();
  }

private:
  std::vector<RenderBatch> m_pool;
  std::vector<uint32_t> m_order;
  std::size_t m_active = 0;
};

} // namespace componeng::renderer
