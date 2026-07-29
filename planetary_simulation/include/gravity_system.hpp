#pragma once

#include "componeng/ecs/system.hpp"

namespace planetary_sim {

class GravitySystem : public componeng::ecs::ISystem {
public:
    void onUpdate(const componeng::ecs::SystemState &state) override;
};

}

