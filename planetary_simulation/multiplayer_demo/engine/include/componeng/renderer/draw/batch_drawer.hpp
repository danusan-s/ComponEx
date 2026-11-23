#pragma once

#include "componeng/core/types.hpp"
#include "componeng/renderer/backend/api/pass_id.hpp"
#include "componeng/renderer/batching/render_queue.hpp"
#include "componeng/renderer/pipeline/pass_context.hpp"

#include <cstddef>

namespace componeng::renderer {

class Shader;

/**
 * @brief Issues the instanced draw for one RenderBatch in a given pass:
 * shader, pipeline state, material data, mesh, instance attributes, draw call,
 * and the pass's counters.
 *
 * The queue is sorted by shader, then texture, so consecutive batches often
 * share GPU state. The drawer remembers what it left bound and skips the
 * redundant work. That memory is only trusted while the drawer is the only
 * thing touching GL, so use one drawer per pass: anything else that binds a
 * shader (the sky pass, ImGui) between passes can't make it stale.
 */
class BatchDrawer {
public:
  /** Reads the cache, binder and `depthPrepassed` (shaded only where the
   *  batch is the nearest surface, see renderStateFor) from `ctx`. */
  explicit BatchDrawer(const PassContext &ctx);

  /** `pass` is DepthPrepass, Opaque or Transparent. The pre-pass runs the
   *  shader's depth-only variant, or for a Cutout material its alpha-tested
   *  one (Shader::alphaTestedDepthVariant). */
  void draw(const RenderBatch &batch, api::PassId pass);

private:
  PassContext m_ctx;

  // The program itself, not its shader id: in the pre-pass an opaque and a
  // cutout material of one shader use different programs.
  const Shader *m_boundProgram = nullptr;
  core::HandleID m_boundAlbedo = core::INVALID_HANDLE;
};

/** Draws the queue's sorted batches [first, last) in `pass` with a fresh
 *  BatchDrawer. The queue is sorted opaque first, transparent last, so a pass
 *  picks its range from RenderQueue::opaqueCount(). */
void drawBatches(const PassContext &ctx, api::PassId pass, std::size_t first,
                 std::size_t last);

} // namespace componeng::renderer
