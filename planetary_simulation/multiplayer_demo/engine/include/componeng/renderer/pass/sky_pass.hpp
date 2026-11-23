#pragma once

#include "componeng/renderer/pass/render_pass.hpp"

namespace componeng::renderer {

/**
 * @brief Draws the sky: a full-screen triangle behind everything, from the
 * first SkyboxComponent in the world (none: draws nothing).
 *
 * Run it after the opaque geometry, not first: the sky shader is expensive,
 * and with the depth test on, the pixels the opaque geometry already covers
 * are rejected before it runs. It must still come before anything blended or
 * drawn without a depth test, which has to land on top of it.
 */
class SkyPass final : public RenderPass {
public:
  api::PassId id() const override {
    return api::PassId::Sky;
  }
  void execute(const PassContext &ctx) override;
};

} // namespace componeng::renderer
