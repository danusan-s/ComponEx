#pragma once

#include "componeng/ecs/entity.hpp"

#include <assert.h>
#include <queue>

namespace componeng::ecs {

/**
 * @brief Metadata stored per entity: its archetype row and component signature.
 */
struct EntityRecord {
  std::uint32_t row = 0;
  std::uint32_t generation = 0; // Even = destroyed, Odd = alive
  ArchetypeID archetypeId = INVALID_ARCHETYPE;
};

/**
 * @brief Manages entity creation, destruction, and ID recycling.
 *
 * Maintains a pool of free entity IDs and a fixed-size array of EntityRecords.
 * When an entity is destroyed its ID is returned to the free pool for reuse.
 */
class EntityManager {
private:
  std::queue<EntityID> m_freeIDs;
  std::vector<EntityRecord> m_entityRecords;

  bool isInBounds(EntityID id) const {
    return id < m_entityRecords.size();
  }

public:
  EntityManager() {
    m_entityRecords.push_back(
        EntityRecord{}); // Reserve index 0 for INVALID_ENTITY
  }

  EntityID createEntity() {
    assert(getLivingEntityCount() < MAX_ENTITIES &&
           "Too many entities in existence.");

    EntityID newID{};

    if (m_freeIDs.empty()) {
      m_entityRecords.push_back(EntityRecord{});
      newID = static_cast<EntityID>(m_entityRecords.size() - 1);
    } else {
      newID = m_freeIDs.front();
      m_freeIDs.pop();
    }

    EntityRecord &record = m_entityRecords[newID];
    record.row = 0;
    record.archetypeId = INVALID_ARCHETYPE;
    record.generation++;

    return newID;
  }

  void destroyEntity(EntityID id) {
    assert(isInBounds(id) && "Entity out of range.");

    EntityRecord &record = m_entityRecords[id];

    if (record.generation % 2 == 0) {
      // Already destroyed
      return;
    }

    record.row = 0;
    record.generation++;
    record.archetypeId = INVALID_ARCHETYPE;
    m_freeIDs.push(id);
  }

  EntityRecord getRecord(EntityID id) {
    assert(isInBounds(id) && "Entity out of range.");
    return m_entityRecords[id];
  }

  void setRecord(EntityID id, const EntityRecord &record) {
    assert(isInBounds(id) && "Entity out of range.");
    m_entityRecords[id] = record;
  }

  size_t getLivingEntityCount() const {
    return m_entityRecords.size() - m_freeIDs.size();
  }
};

} // namespace componeng::ecs
