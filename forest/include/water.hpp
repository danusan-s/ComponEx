#pragma once

#include "componeng/core/types.hpp"
#include "componeng/ecs/system.hpp"

#include <memory>

namespace componeng::renderer {
class Mesh;
}

namespace forest {

// The sea: a flat grid in the xz plane, centred on the origin at y = 0 (the
// entity's transform lifts it to cfg::kWaterLevel). It is much larger than the
// world, so it runs out to the fogged horizon, and finely enough divided
// (cfg::kWaterCells) for a vertex shader to ripple it later. UV carries the
// position in metres.
std::unique_ptr<componeng::renderer::Mesh> buildWaterPlane();

// Height of the water's surface at (x, z) and time t (seconds): the still level
// plus the long swells that water.vert moves the geometry by (same waves, so the
// CPU and the GPU agree on where the surface is).
float waterSurfaceHeight(float x, float z, float t);

// How far `p` is below the water's surface, in metres (negative above it). Only
// meaningful inside the sea's extent, which is the whole playable world.
float depthBelowSurface(const componeng::core::Vec3 &p, float t);

// Under the surface the world turns dark blue and murky. The fog is a shared
// resource that every forest shader already applies, so this blends it (and the
// sky and the sun, which the fog and sky shaders also read) towards "underwater"
// by how deep the camera is, instead of touching any shader's lighting.
class UnderwaterSystem : public componeng::ecs::ISystem {
public:
  void onUpdate(const componeng::ecs::SystemState &state) override;

private:
  // What the world looked like above the water, captured on the first frame.
  struct Above {
    componeng::core::Vec4 fogColor;
    float fogDensity;
    componeng::core::Vec3 skyColor, sunColor;
  } m_above{};
  bool m_captured = false;
};

} // namespace forest
