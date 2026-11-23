#pragma once

namespace componeng::resources {

enum class AntiAliasing {
  None,
  Fxaa, // post-process edge smoothing, see PostProcessPass
};

/**
 * @brief Renderer switches the RenderSystem reads every frame.
 */
struct RenderSettings {
  /**
   * Draw opaque geometry twice: first depth only, with a trivial fragment
   * shader, then shaded with the depth test set to "equal" so only the
   * nearest surface of each pixel runs the real fragment shader. Pays an extra
   * vertex pass to save fragment work, so it wins when overdraw or fragment
   * cost is high. Vertex shaders must declare `invariant gl_Position;` so both
   * passes compute bit-identical depth. Alpha-tested materials
   * (MaterialType::Cutout) take part too, with an alpha-testing depth shader.
   */
  bool depthPrepass = true;

  /**
   * Cross-fade between LodComponent levels instead of switching in one frame:
   * both meshes are drawn, each discarding the complement of the other's
   * dither pattern (no blending, no sorting), at the cost of one extra
   * instance per fading entity. Needs a shader with `instanceFade` (the
   * bundled ones have it); other materials keep the hard switch.
   */
  bool lodDither = true;

  /** How long a LOD cross-fade lasts, in seconds. 0 switches instantly. */
  float lodFadeSeconds = 0.35f;

  /**
   * Fxaa draws the scene into an off-screen target, then smooths its edges
   * while copying it to the window. It costs
   * one extra full-screen pass, works on every backend, and also softens the
   * edges multisampling would miss (alpha-tested leaves, LOD dithering). It
   * needs the "post_process" shader (RenderPlugin loads it); without one it is
   * off.
   */
  AntiAliasing antiAliasing = AntiAliasing::Fxaa;

  /**
   * Size of the scene as drawn, relative to the window's viewport: 0.5 draws a
   * quarter of the pixels and scales it up, 2 draws four times as many and
   * scales it down (supersampling). Anything but 1 draws the scene into an
   * off-screen target, like anti-aliasing does, and the same final pass copies
   * it to the window with linear filtering. Clamped to 0.25 .. 2. Needs the
   * "post_process" shader too.
   */
  float renderScale = 1.0f;
};

} // namespace componeng::resources
