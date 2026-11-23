#pragma once

#include "componeng/ecs/system.hpp"

#include <memory>

namespace componeng::renderer {
class Mesh;
}

namespace forest {

// Marks a tree's billboard: shown once the 3D tree has been replaced by it.
// (The engine's LodComponent has three levels, each with its own mesh but one
// material; a billboard needs its own material and a minimum distance, so it is
// switched here instead.)
//
// Like the engine's LodComponent it works in one of two modes. With `size` > 0
// the billboard shows when the tree covers less of the screen's height than
// `screenHeight` (size * scale / (2 * distance * tan(fov / 2)), the same measure
// as LodComponent's screen-height mode), so it matches the last 3D level's
// threshold. Otherwise it shows beyond `nearDistance` metres.
struct BillboardLodComponent {
  static constexpr const char *component_name = "BillboardLodComponent";
  float nearDistance = 0.0f;
  float size = 0.0f;
  float screenHeight = 0.0f;
};

// Distance beyond which trees are billboards. FOREST_BILLBOARD_NEAR overrides
// it, to look at the billboards up close.
float billboardNearDistance();

// A quad in a tree's model space: x in [-halfWidth, halfWidth], y in
// [bottom, top], uv (0, 0) at the top left. billboard.vert turns it to face the
// camera.
std::unique_ptr<componeng::renderer::Mesh> buildBillboardQuad(float halfWidth, float bottom,
                                                              float top);

// Shows each billboard once the camera is far enough away. Runs before the
// engine's CullingSystem.
class BillboardLodSystem : public componeng::ecs::ISystem {
public:
  void onUpdate(const componeng::ecs::SystemState &state) override;
};

} // namespace forest
