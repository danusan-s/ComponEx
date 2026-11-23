#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace componeng::ecs {

using EntityID = std::uint32_t;

using ComponentID = std::uint8_t;

using ArchetypeID = std::uint16_t;

constexpr std::size_t MAX_ENTITIES = 100000;

constexpr std::size_t MAX_COMPONENTS = 64;

constexpr std::size_t MAX_ARCHETYPES = 256;

constexpr EntityID INVALID_ENTITY = 0;

// An entity with no components belongs to no archetype.
constexpr ArchetypeID INVALID_ARCHETYPE =
    static_cast<ArchetypeID>(MAX_ARCHETYPES);

static_assert(MAX_ENTITIES <= std::numeric_limits<EntityID>::max(),
              "EntityID cannot address every entity");
static_assert(MAX_ARCHETYPES < std::numeric_limits<ArchetypeID>::max(),
              "ArchetypeID must represent every archetype and still leave "
              "INVALID_ARCHETYPE distinct from a real id");
static_assert(MAX_COMPONENTS < std::numeric_limits<ComponentID>::max(),
              "ComponentID cannot index every component");

using Signature = std::bitset<MAX_COMPONENTS>;

} // namespace componeng::ecs
