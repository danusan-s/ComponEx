#pragma once

#include <memory>

namespace componeng::renderer {
class Mesh;
}

namespace forest {

// A tuft of tapered, leaning blades as one mesh, instanced for every tuft.
std::unique_ptr<componeng::renderer::Mesh> buildGrassTuft();

// The grass field is one entity (ProceduralInstancesComponent) drawing a pool of
// kTuftSide * kTuftSide tufts. grass.vert places each from its instance id: a
// jittered grid of kPeriod metres wrapped around the camera, so the player sees
// grass everywhere while the GPU only draws the pool. These must match what the
// shader is told through material uniforms (see loadForestAssets).
struct GrassField {
  static constexpr float kPeriod = 144.0f;
  static constexpr int kTuftSide = 432;  // ~9 tufts per square metre
  static constexpr float kFadeStart = 48.0f;  // tufts shrink to nothing between
  static constexpr float kFadeEnd = 70.0f;    // these distances
  static constexpr int kTuftCount = kTuftSide * kTuftSide;
};

} // namespace forest
