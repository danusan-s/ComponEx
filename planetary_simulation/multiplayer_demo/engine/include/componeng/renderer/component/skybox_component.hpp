#pragma once

#include "componeng/core/types.hpp"

namespace componeng::renderer {

/**
 * @brief Marks an entity as the sky. The RenderSystem draws the first one it
 * finds, before everything else, as a full-screen triangle behind the scene.
 *
 * The sky is drawn with `shaderName`, so it can be replaced with any shader
 * that follows the sky contract (see docs/GAME_GUIDE.md): it receives
 * `invSkyViewProj`, `skyColor` and `horizonColor` as loose uniforms, plus the
 * per-frame values (light direction and colour, fog, camera) either from the
 * FrameData block or as loose uniforms, like any other shader.
 */
struct SkyboxComponent {
  static constexpr const char *component_name = "SkyboxComponent";

  // Shader used to draw the sky; the built-in procedural sky by default.
  core::Name shaderName = core::Name("skybox");

  // Colour straight overhead.
  core::Vec3 color = core::Vec3(0.5f, 0.7f, 1.0f);

  // Colour at the horizon. By default the fog colour is used instead, so
  // distant fogged geometry fades into the sky with no visible seam.
  core::Vec3 horizonColor = core::Vec3(0.7f, 0.8f, 0.95f);
  bool horizonFromFog = true;
};

} // namespace componeng::renderer
