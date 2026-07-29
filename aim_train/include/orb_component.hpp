#pragma once

namespace aim_train {

struct OrbComponent {
    static constexpr const char *component_name = "OrbComponent";
    float radius;
    float speed;
    float phase;
    float centerY;
};

}
