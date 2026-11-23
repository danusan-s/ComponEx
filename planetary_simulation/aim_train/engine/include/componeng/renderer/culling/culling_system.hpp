#pragma once

#include "componeng/ecs/system.hpp"

namespace componeng::renderer {

/**
 * @brief Presentation-phase system that frustum culling
 */
class CullingSystem final : public ecs::ISystem {
public:
  void onCreate(const ecs::SystemState &state) override;
  void onUpdate(const ecs::SystemState &state) override;

private:
};

} // namespace componeng::renderer
