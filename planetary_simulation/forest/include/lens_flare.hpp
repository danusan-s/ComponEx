#pragma once

#include "componeng/core/types.hpp"
#include "componeng/ecs/system.hpp"

#include <memory>

namespace componeng::renderer {
class Mesh;
}

namespace forest {

// A unit quad (xy in [-1, 1], facing +z) for the flare's sprites.
std::unique_ptr<componeng::renderer::Mesh> buildFlareQuad();

// Number of instances the flare shader expects: glare, veil, halo, six ghosts.
inline constexpr int kFlareElements = 9;

// Feeds the flare shader whether the sun can be seen from the camera.
//
// The flare itself is drawn by one procedural entity (see flare.vert), but a
// shader cannot know whether trees or hills hide the sun: the engine offers no
// depth read-back or occlusion query. So each frame this traces a ray from the
// camera to the sun through the world (ForestWorld::sunVisibility) and passes
// the result, smoothed so a crown drifting across the sun fades the flare
// instead of popping it, as the material uniform `sunVisible`.
class LensFlareSystem : public componeng::ecs::ISystem {
public:
  void onCreate(const componeng::ecs::SystemState &state) override;
  void onUpdate(const componeng::ecs::SystemState &state) override;

private:
  componeng::core::HandleID m_material = 0;
  float m_visibility = 0.0f;
  bool m_first = true;
};

} // namespace forest
