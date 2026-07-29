#pragma once

namespace planetary_sim {

struct PlanetComponent {
    static constexpr const char *component_name = "PlanetComponent";
    float mass;
    float orbitRadius;
    float orbitSpeed;
    float phase;
    float rotationSpeed;
};

struct StarComponent {
    static constexpr const char *component_name = "StarComponent";
    float mass;
};

}

