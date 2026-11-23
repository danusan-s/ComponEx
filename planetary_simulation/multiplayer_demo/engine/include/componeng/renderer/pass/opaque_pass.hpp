#pragma once

#include "componeng/renderer/pass/render_pass.hpp"

namespace componeng::renderer {

/**
 * @brief Draws the opaque and cutout batches, shaded. After a depth pre-pass
 * it shades only the pixel's nearest surface (see renderStateFor).
 */
class OpaquePass final : public RenderPass {
public:
  api::PassId id() const override {
    return api::PassId::Opaque;
  }
  void execute(const PassContext &ctx) override;
};

} // namespace componeng::renderer
