#pragma once

#include "componeng/core/types.hpp"
#include "componeng/ecs/system.hpp"

namespace aim_train {

bool raySphereIntersect(componeng::core::Vec3 origin, componeng::core::Vec3 dir,
                        componeng::core::Vec3 center, float radius, float &t);

class AimSystem : public componeng::ecs::ISystem {
    int m_score = 0;
public:
    void onUpdate(const componeng::ecs::SystemState &state) override;
};

}
