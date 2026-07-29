#pragma once

#include "componeng/ecs/system.hpp"

namespace planetary_sim {

class CameraController : public componeng::ecs::ISystem {
    float m_sensitivity = 0.25f;
    float m_moveSpeed = 10.0f;

public:
    void onUpdate(const componeng::ecs::SystemState &state) override;
};

}

