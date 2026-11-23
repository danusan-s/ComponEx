#pragma once

#include "componeng/ecs/system.hpp"

namespace componeng::camera {

/**
 * @brief Simulation-phase system that processes camera movement and mouse look.
 *
 * Updates the camera entity's TransformComponent and computes the
 * view/projection matrix in CameraComponent.
 */
class CameraSystem final : public ecs::ISystem {
public:
  void onUpdate(const ecs::SystemState &state) override;
};

} // namespace componeng::camera
