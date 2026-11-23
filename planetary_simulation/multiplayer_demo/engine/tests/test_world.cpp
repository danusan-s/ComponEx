#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/entity.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <vector>

using namespace componeng::core;
using namespace componeng::ecs;
using namespace componeng::renderer;

namespace {

// World holds a MAX_ENTITIES-sized record array and MAX_ARCHETYPES archetypes,
// so it is heap-allocated rather than placed on the test stack.
std::unique_ptr<World> makeWorld() {
  auto world = std::make_unique<World>();
  world->registerComponents<TransformComponent, MeshComponent>();
  return world;
}

EntityID spawn(World &world, float x) {
  EntityID entity = world.createEntity();
  world.addComponents(entity,
                      TransformComponent{.position = Vec3(x, 0.0f, 0.0f),
                                         .rotation = Vec3(0.0f),
                                         .scale = Vec3(1.0f)});
  return entity;
}

} // namespace

// Regression: destroyEntity used to discard Archetype::removeEntity's return
// value, leaving the swapped-in entity's record pointing at its old row.
//
// Note the vacated row must be *reused* before asserting. Swap-remove copies
// the last row down and decrements the count without scrubbing the tail, so a
// stale record keeps reading its own old bytes and the bug stays invisible
// until something else claims that row.
TEST(WorldTest, DestroyEntityRepointsSwappedEntity) {
  auto world = makeWorld();

  EntityID a = spawn(*world, 1.0f);
  EntityID b = spawn(*world, 2.0f);
  EntityID c = spawn(*world, 3.0f);

  // Removing row 0 swap-moves the last row (entity c) into it.
  world->destroyEntity(a);

  // Claims the row c used to occupy.
  EntityID d = spawn(*world, 4.0f);

  EXPECT_FLOAT_EQ(world->getComponent<TransformComponent>(c).position.x, 3.0f);
  EXPECT_FLOAT_EQ(world->getComponent<TransformComponent>(b).position.x, 2.0f);
  EXPECT_FLOAT_EQ(world->getComponent<TransformComponent>(d).position.x, 4.0f);
}

TEST(WorldTest, DestroyEveryEntityLeavesArchetypeEmpty) {
  auto world = makeWorld();

  std::vector<EntityID> entities;
  for (int i = 0; i < 16; ++i) {
    entities.push_back(spawn(*world, static_cast<float>(i)));
  }

  for (EntityID entity : entities) {
    world->destroyEntity(entity);
  }

  int visited = 0;
  world->query().read<TransformComponent>().each(
      [&](const TransformComponent &) { ++visited; });
  EXPECT_EQ(visited, 0);
}

// The editor's "clear scene" / "delete selected" shape: interleaved destroys
// and spawns, so every freed row gets reclaimed by a later entity.
TEST(WorldTest, InterleavedDestroyAndSpawnKeepsRecordsValid) {
  auto world = makeWorld();

  std::vector<EntityID> entities;
  std::vector<float> expected;
  for (int i = 0; i < 8; ++i) {
    entities.push_back(spawn(*world, static_cast<float>(i)));
    expected.push_back(static_cast<float>(i));
  }

  // Destroy from the middle, then refill, three times over.
  for (int round = 0; round < 3; ++round) {
    std::size_t victim = 2;
    world->destroyEntity(entities[victim]);
    entities.erase(entities.begin() + victim);
    expected.erase(expected.begin() + victim);

    float value = 100.0f + round;
    entities.push_back(spawn(*world, value));
    expected.push_back(value);
  }

  for (std::size_t i = 0; i < entities.size(); ++i) {
    EXPECT_FLOAT_EQ(
        world->getComponent<TransformComponent>(entities[i]).position.x,
        expected[i])
        << "entity index " << i;
  }
}

// Adding a component migrates the entity to a new archetype, which swap-removes
// from the old one — the same row-fixup path as destroyEntity.
TEST(WorldTest, AddComponentRepointsSwappedEntity) {
  auto world = makeWorld();

  EntityID a = spawn(*world, 1.0f);
  EntityID b = spawn(*world, 2.0f);

  world->addComponent(a, MeshComponent{.meshName = "cube"});

  EXPECT_FLOAT_EQ(world->getComponent<TransformComponent>(b).position.x, 2.0f);
  EXPECT_FLOAT_EQ(world->getComponent<TransformComponent>(a).position.x, 1.0f);
}

TEST(WorldTest, RemoveComponentRepointsSwappedEntity) {
  auto world = makeWorld();

  EntityID a = world->createEntity();
  world->addComponents(a,
                       TransformComponent{.position = Vec3(1.0f, 0.0f, 0.0f),
                                          .rotation = Vec3(0.0f),
                                          .scale = Vec3(1.0f)},
                       MeshComponent{.meshName = "cube"});
  EntityID b = world->createEntity();
  world->addComponents(b,
                       TransformComponent{.position = Vec3(2.0f, 0.0f, 0.0f),
                                          .rotation = Vec3(0.0f),
                                          .scale = Vec3(1.0f)},
                       MeshComponent{.meshName = "sphere"});

  world->removeComponent<MeshComponent>(a);

  EXPECT_FLOAT_EQ(world->getComponent<TransformComponent>(b).position.x, 2.0f);
  EXPECT_EQ(world->getComponent<MeshComponent>(b).meshName, "sphere");
}
