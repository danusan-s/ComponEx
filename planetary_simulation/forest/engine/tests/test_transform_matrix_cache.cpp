#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/renderer/transform_matrix_cache.hpp"
#include "gtest/gtest.h"

#include <map>
#include <memory>
#include <stdexcept>

using namespace componeng::core;
using namespace componeng::ecs;
using namespace componeng::renderer;

namespace {

struct Tag {
  static constexpr const char *component_name = "Tag";
};

std::unique_ptr<World> makeWorld() {
  auto world = std::make_unique<World>();
  world->registerComponents<TransformComponent, MeshComponent, Tag>();
  return world;
}

EntityID spawn(World &world, float x, bool withMesh = true) {
  EntityID e = world.createEntity();
  world.addComponents(e, TransformComponent{.position = Vec3(x, 0.0f, 0.0f),
                                            .rotation = Vec3(0.0f),
                                            .scale = Vec3(1.0f)});
  if (withMesh)
    world.addComponent(e, MeshComponent{});
  return e;
}

// Refreshes the cache over every Transform+Mesh archetype and reads back each
// entity's matrix.
std::map<EntityID, Mat4> refreshAndRead(World &world,
                                        TransformMatrixCache &cache) {
  std::map<EntityID, Mat4> out;
  world.query().read<TransformComponent, MeshComponent>().eachChunk(
      [&](const auto &chunk) {
        cache.refresh(chunk);
        const Mat4 *models = cache.models(chunk.archetypeId());
        for (std::size_t row = chunk.begin(); row < chunk.end(); ++row)
          out[chunk.entity(row)] = models[row];
      });
  return out;
}

float tx(const Mat4 &m) {
  return m[3][0];
}

// Writes without taking write access, i.e. without bumping the column's
// version. This is exactly what the cache's contract says it cannot see, so it
// lets the tests observe whether a refresh did any per-row work.
void writeBehindTheEcsBack(World &world, EntityID e, float x) {
  const_cast<TransformComponent &>(world.readComponent<TransformComponent>(e))
      .position.x = x;
}

} // namespace

TEST(TransformMatrixCache, BuildsTheSameMatrixAsToMatrix) {
  auto world = makeWorld();
  EntityID e = world->createEntity();
  const TransformComponent t{{1, 2, 3}, {10, 20, 30}, {1, 2, 3}};
  world->addComponents(e, TransformComponent(t), MeshComponent{});

  TransformMatrixCache cache;
  const Mat4 got = refreshAndRead(*world, cache).at(e);
  const Mat4 want = t.toMatrix();
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_NEAR(got[c][r], want[c][r], 1e-4f);
}

TEST(TransformMatrixCache, UnchangedVersionSkipsTheArchetypeEntirely) {
  auto world = makeWorld();
  EntityID e = spawn(*world, 1.0f);

  TransformMatrixCache cache;
  EXPECT_FLOAT_EQ(tx(refreshAndRead(*world, cache).at(e)), 1.0f);

  // Nothing bumped the version, so the refresh must not even look at rows.
  writeBehindTheEcsBack(*world, e, 9.0f);
  EXPECT_FLOAT_EQ(tx(refreshAndRead(*world, cache).at(e)), 1.0f);
}

TEST(TransformMatrixCache, ReadOnlyAccessDoesNotInvalidate) {
  auto world = makeWorld();
  EntityID e = spawn(*world, 1.0f);

  TransformMatrixCache cache;
  refreshAndRead(*world, cache);
  writeBehindTheEcsBack(*world, e, 9.0f);

  // None of these may bump the Transform column.
  (void)world->readComponent<TransformComponent>(e);
  (void)world->tryReadComponent<TransformComponent>(e);
  world->query().read<TransformComponent>().each(
      [](const TransformComponent &) {});

  EXPECT_FLOAT_EQ(tx(refreshAndRead(*world, cache).at(e)), 1.0f);
}

TEST(TransformMatrixCache, GetComponentInvalidates) {
  auto world = makeWorld();
  EntityID e = spawn(*world, 1.0f);

  TransformMatrixCache cache;
  refreshAndRead(*world, cache);

  world->getComponent<TransformComponent>(e).position.x = 5.0f;
  EXPECT_FLOAT_EQ(tx(refreshAndRead(*world, cache).at(e)), 5.0f);
}

TEST(TransformMatrixCache, WriteQueryInvalidates) {
  auto world = makeWorld();
  EntityID e = spawn(*world, 1.0f);

  TransformMatrixCache cache;
  refreshAndRead(*world, cache);

  world->query().write<TransformComponent>().each(
      [](TransformComponent &t) { t.position.x = 7.0f; });
  EXPECT_FLOAT_EQ(tx(refreshAndRead(*world, cache).at(e)), 7.0f);
}

TEST(TransformMatrixCache, ChangedVersionRebuildsOnlyWhatMoved) {
  auto world = makeWorld();
  EntityID a = spawn(*world, 1.0f);
  EntityID b = spawn(*world, 2.0f);

  TransformMatrixCache cache;
  refreshAndRead(*world, cache);

  // `b` is changed without a bump; `a` is changed with one. The bump forces a
  // per-row compare of the whole archetype, which then picks up both: a row
  // whose floats differ is always rebuilt.
  writeBehindTheEcsBack(*world, b, 20.0f);
  world->getComponent<TransformComponent>(a).position.x = 10.0f;

  auto m = refreshAndRead(*world, cache);
  EXPECT_FLOAT_EQ(tx(m.at(a)), 10.0f);
  EXPECT_FLOAT_EQ(tx(m.at(b)), 20.0f);
}

TEST(TransformMatrixCache, MatricesFollowRowsAfterSwapRemove) {
  auto world = makeWorld();
  EntityID a = spawn(*world, 1.0f);
  EntityID b = spawn(*world, 2.0f);
  EntityID c = spawn(*world, 3.0f);

  TransformMatrixCache cache;
  refreshAndRead(*world, cache);

  // Removing row 0 swap-moves `c` into it: the rows' contents shuffle.
  world->destroyEntity(a);
  EntityID d = spawn(*world, 4.0f);

  auto m = refreshAndRead(*world, cache);
  ASSERT_EQ(m.size(), 3u);
  EXPECT_FLOAT_EQ(tx(m.at(b)), 2.0f);
  EXPECT_FLOAT_EQ(tx(m.at(c)), 3.0f);
  EXPECT_FLOAT_EQ(tx(m.at(d)), 4.0f);
}

TEST(TransformMatrixCache, EntityMovingArchetypesKeepsItsMatrix) {
  auto world = makeWorld();
  EntityID e = spawn(*world, 6.0f, /*withMesh=*/false);

  TransformMatrixCache cache;
  EXPECT_TRUE(refreshAndRead(*world, cache).empty()); // no Mesh: not covered

  world->addComponent(e, MeshComponent{}); // moves to {Transform, Mesh}
  EXPECT_FLOAT_EQ(tx(refreshAndRead(*world, cache).at(e)), 6.0f);
}

TEST(TransformMatrixCache, ArchetypesAreIndependent) {
  auto world = makeWorld();
  EntityID plain = spawn(*world, 1.0f); // {Transform, Mesh}
  EntityID tagged = spawn(*world, 2.0f);
  world->addComponent(tagged, Tag{}); // moves to {Transform, Mesh, Tag}

  TransformMatrixCache cache;
  refreshAndRead(*world, cache);

  writeBehindTheEcsBack(*world, plain, 10.0f);
  writeBehindTheEcsBack(*world, tagged, 20.0f);

  // Bump only the plain archetype's Transform column.
  (void)world->getComponent<TransformComponent>(plain);

  auto m = refreshAndRead(*world, cache);
  EXPECT_FLOAT_EQ(tx(m.at(plain)), 10.0f); // its version moved: rows compared
  EXPECT_FLOAT_EQ(tx(m.at(tagged)), 2.0f); // its version did not: skipped
}

TEST(TransformMatrixCache, ModelsThrowsForAnArchetypeNeverRefreshed) {
  TransformMatrixCache cache;
  EXPECT_THROW(cache.models(0), std::runtime_error);
}

TEST(TransformMatrixCache,
     RefreshHelperCreatesTheSharedResourceAndCoversMeshes) {
  auto world = makeWorld();
  EntityID e = spawn(*world, 3.0f);

  EXPECT_FALSE(world->hasResource<TransformMatrixCache>());
  TransformMatrixCache &cache = refreshTransformMatrices(*world);
  EXPECT_TRUE(world->hasResource<TransformMatrixCache>());
  EXPECT_EQ(&cache, &world->getResource<TransformMatrixCache>());

  float found = -1.0f;
  world->query().read<TransformComponent, MeshComponent>().eachChunk(
      [&](const auto &chunk) {
        const Mat4 *models = cache.models(chunk.archetypeId());
        for (std::size_t row = chunk.begin(); row < chunk.end(); ++row)
          if (chunk.entity(row) == e)
            found = tx(models[row]);
      });
  EXPECT_FLOAT_EQ(found, 3.0f);

  // A second call, as the other renderer system makes, is idempotent.
  EXPECT_EQ(&refreshTransformMatrices(*world), &cache);
}
