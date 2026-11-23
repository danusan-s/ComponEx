#pragma once

#include "componeng/renderer/pass/render_pass.hpp"

#include <memory>
#include <vector>

namespace componeng::renderer {

/**
 * @brief The passes that draw a frame, in the order they run.
 *
 * Order matters, and is set by whoever builds the pipeline (RenderSystem):
 * e.g. the sky goes after the opaque geometry but before anything blended.
 */
class RenderPipeline {
public:
  /** Appends `pass` to run after the ones already added. */
  RenderPipeline &add(std::unique_ptr<RenderPass> pass);

  /** Runs every enabled pass in order, each bracketed for timing. */
  void execute(const PassContext &ctx) const;

private:
  std::vector<std::unique_ptr<RenderPass>> m_passes;
};

} // namespace componeng::renderer
