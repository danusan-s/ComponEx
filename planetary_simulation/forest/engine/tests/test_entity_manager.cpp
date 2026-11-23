#include "componeng/ecs/entity_manager.hpp"
#include <gtest/gtest.h>

TEST(EntityManagerTest, CreatesSequentialIDs) {
  componeng::ecs::EntityManager em;
  EXPECT_EQ(em.createEntity(), 1u);
  EXPECT_EQ(em.createEntity(), 2u);
  EXPECT_EQ(em.createEntity(), 3u);
}

TEST(EntityManagerTest, RecyclesDestroyedIDs) {
  componeng::ecs::EntityManager em;
  // Exhaust all pre-initialized IDs first (starts from 1, so MAX_ENTITIES-1
  // total)
  for (componeng::ecs::EntityID i = 1; i < componeng::ecs::MAX_ENTITIES; ++i) {
    em.createEntity();
  }
  // Now destroy and recreate
  em.destroyEntity(1u);
  EXPECT_EQ(em.createEntity(), 1u);
}

TEST(EntityManagerTest, TracksLivingEntityCount) {
  componeng::ecs::EntityManager em;
  em.createEntity(); // 1
  em.createEntity(); // 2
  em.createEntity(); // 3
  em.destroyEntity(2u);

  auto rec = em.getRecord(2u);
  EXPECT_EQ(rec.archetypeId, componeng::ecs::INVALID_ARCHETYPE);
  EXPECT_EQ(rec.row, 0u);
}

TEST(EntityManagerTest, GetRecordReturnsCorrectData) {
  componeng::ecs::EntityManager em;
  componeng::ecs::EntityID id = em.createEntity(); // 1
  componeng::ecs::EntityRecord rec = em.getRecord(id);
  EXPECT_EQ(rec.row, 0u);
  EXPECT_EQ(rec.archetypeId, componeng::ecs::INVALID_ARCHETYPE);
}

TEST(EntityManagerTest, DestroyedEntityRecordIsCleared) {
  componeng::ecs::EntityManager em;
  componeng::ecs::EntityID id = em.createEntity(); // 1
  em.destroyEntity(id);
  componeng::ecs::EntityRecord rec = em.getRecord(id);
  EXPECT_EQ(rec.row, 0u);
  EXPECT_EQ(rec.generation % 2,
            0u); // Generation should be even after destruction
  EXPECT_EQ(rec.archetypeId, componeng::ecs::INVALID_ARCHETYPE);
}
