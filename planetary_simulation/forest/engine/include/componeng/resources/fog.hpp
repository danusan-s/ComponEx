#pragma once

#include "componeng/core/types.hpp"

namespace componeng::resources {

/**
 * @brief Fog settings for atmospheric scattering.
 *
 * The RenderSystem reads this resource and passes fogColor/fogDensity/
 * fogStart/fogEnd to shaders. Distance-based exponential fog blends
 * distant geometry toward fogColor.
 */
struct Fog {
  core::Vec4 color = core::Vec4(0.5f, 0.7f, 1.0f, 1.0f); // sky-like fog
  float density = 0.002f;                                // exponential density
  float start = 50.0f; // linear fog start distance
  float end = 800.0f;  // linear fog end distance
  bool enabled = true;
};

} // namespace componeng::resources
