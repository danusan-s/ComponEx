#include "componeng/ecs/world.hpp"

#include "componeng/events/entity_event.hpp"
#include "componeng/utils/logger.hpp"

namespace componeng::ecs {

EntityID World::createEntity() {
  EntityID entity = m_entityManager.createEntity();
  m_eventBus.emit<events::EntityCreateEvent>({.entity = entity});
  return entity;
}

void World::destroyEntity(EntityID entity) {
  m_eventBus.emit<events::EntityDestroyEvent>({.entity = entity});
  EntityRecord record = m_entityManager.getRecord(entity);
  Archetype *currArchetype = archetypeOf(record);
  if (currArchetype) {
    // Archetype storage is swap-remove: the entity previously occupying the
    // last row is moved into the freed row, so its record must be repointed
    // or every later access reads the wrong (or an out-of-bounds) row.
    std::size_t freedRow = record.row;
    EntityID moved = currArchetype->removeEntityAtRow(freedRow);
    if (moved != INVALID_ENTITY) {
      EntityRecord movedRecord = m_entityManager.getRecord(moved);
      movedRecord.row = freedRow;
      m_entityManager.setRecord(moved, movedRecord);
    }
  }
  m_entityManager.destroyEntity(entity);
}

void World::createSystems() {
  m_systemManager.createAll(this);
}

void World::updateSystems(float deltaTime) {
  m_systemManager.updateAll(this, deltaTime);
  m_eventBus.swapBuffers();
}

void World::destroySystems() {
  m_systemManager.destroyAll(this);
}

ThreadPool &World::threadPool() {
  return m_threadPool;
}

renderer::api::IRenderDevice &World::getRenderDevice() {
  return *getResource<std::unique_ptr<renderer::api::IRenderDevice>>().get();
}

double World::getTime() const {
  return time;
}

} // namespace componeng::ecs
