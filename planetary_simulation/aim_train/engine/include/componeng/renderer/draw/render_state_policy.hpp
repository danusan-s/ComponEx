#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/backend/api/pass_id.hpp"

namespace componeng::renderer {

/**
 * @brief The pipeline state a pass draws with. This is the one place that says
 * what each pass does to culling, depth and blending, so changing a pass's
 * behaviour does not mean reading the draw loop.
 *
 * `material` is the state the batch's material asked for. How much of it a
 * pass honours differs:
 *  - DepthPrepass: only its culling; the rest is fixed (depth in, no colour).
 *  - Opaque:       only its culling. Depth is writable, or after a pre-pass
 *                  read-only and tested for equality, so that only the nearest
 *                  surface of each pixel is shaded. Cutout materials (alpha
 *                  tested) draw in these two passes with exactly this state.
 *  - Sky:          none of it; ignored.
 *  - Transparent:  all of it (blending, depth write, culling).
 *  - PostProcess:  none of it; ignored.
 *
 * `depthPrepassed` says the depth buffer already holds the opaque surfaces.
 */
api::RenderState renderStateFor(api::PassId pass,
                                const api::RenderState &material,
                                bool depthPrepassed);

} // namespace componeng::renderer
