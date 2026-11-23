#pragma once

#include "componeng/ecs/system.hpp"

namespace componeng::renderer {

/**
 * @brief Selects each LodComponent entity's mesh from its distance to the
 * camera, or, if the component has a `size`, from how much of the screen's
 * height the entity covers (see LodComponent).
 *
 * Runs in the Presentation group before CullingSystem, so culling sees the
 * chosen mesh's bounds.
 */
class LodSystem final : public ecs::ISystem {
public:
  void onUpdate(const ecs::SystemState &state) override;
};

} // namespace componeng::renderer
