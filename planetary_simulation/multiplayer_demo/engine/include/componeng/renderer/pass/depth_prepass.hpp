#pragma once

#include "componeng/renderer/pass/render_pass.hpp"

namespace componeng::renderer {

/**
 * @brief Draws the opaque batches again, depth only. The opaque pass then
 * shades just the pixel's nearest surface.
 *
 * Runs only when RenderSystem found a pre-pass worthwhile this frame
 * (PassContext::depthPrepassed): enabled in RenderSettings and some opaque
 * batch exists.
 */
class DepthPrepass final : public RenderPass {
public:
  api::PassId id() const override {
    return api::PassId::DepthPrepass;
  }
  bool enabled(const PassContext &ctx) const override {
    return ctx.depthPrepassed;
  }
  void execute(const PassContext &ctx) override;
};

} // namespace componeng::renderer
