#pragma once

#include "componeng/renderer/pass/render_pass.hpp"

namespace componeng::renderer {

/**
 * @brief Draws the blended and depth-less batches. Runs last so they land on
 * top of everything else, the sky included.
 */
class TransparentPass final : public RenderPass {
public:
  api::PassId id() const override {
    return api::PassId::Transparent;
  }
  void execute(const PassContext &ctx) override;
};

} // namespace componeng::renderer
