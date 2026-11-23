#include "componeng/physics/broad_phase.hpp"

#include "componeng/physics/collision_detection.hpp"
#include "gtest/gtest.h"

#include <random>

using namespace componeng;
using namespace componeng::physics;
using Pairs = std::vector<std::pair<size_t, size_t>>;

namespace {

Aabb box(float x, float y, float z, float h = 1.0f) {
  return {core::Vec3(x - h, y - h, z - h), core::Vec3(x + h, y + h, z + h)};
}

ColliderComponent makeCollider(ColliderType type) {
  ColliderComponent c;
  c.type = type;
  return c;
}

} // namespace

TEST(BroadPhaseTest, EmptyAndSingle) {
  EXPECT_TRUE(sweepAndPrune({}).empty());
  EXPECT_TRUE(sweepAndPrune({box(0, 0, 0)}).empty());
}

TEST(BroadPhaseTest, OverlappingPairFound) {
  Pairs p = sweepAndPrune({box(0, 0, 0), box(1, 0, 0)});
  ASSERT_EQ(p.size(), 1u);
  EXPECT_EQ(p[0], (std::pair<size_t, size_t>{0, 1}));
}

TEST(BroadPhaseTest, SeparatedOnEachAxisPruned) {
  EXPECT_TRUE(sweepAndPrune({box(0, 0, 0), box(10, 0, 0)}).empty());
  // Overlap on X but not Y / Z.
  EXPECT_TRUE(sweepAndPrune({box(0, 0, 0), box(0, 10, 0)}).empty());
  EXPECT_TRUE(sweepAndPrune({box(0, 0, 0), box(0, 0, 10)}).empty());
}

TEST(BroadPhaseTest, TouchingCountsAsOverlap) {
  EXPECT_EQ(sweepAndPrune({box(0, 0, 0), box(2, 0, 0)}).size(), 1u);
}

TEST(BroadPhaseTest, PairsAreOrderedRegardlessOfInputOrder) {
  // Index 0 is furthest along X, so sorted order differs from index order.
  Pairs p = sweepAndPrune(
      {box(1, 0, 0, 0.75f), box(0, 0, 0, 0.75f), box(-1, 0, 0, 0.75f)});
  Pairs expected = {{0, 1}, {1, 2}};
  EXPECT_EQ(p, expected);
}

TEST(BroadPhaseTest, MatchesBruteForce) {
  std::mt19937 rng(1234);
  std::uniform_real_distribution<float> pos(-20.0f, 20.0f);
  std::uniform_real_distribution<float> size(0.2f, 2.0f);

  std::vector<Aabb> bounds;
  for (int i = 0; i < 200; ++i)
    bounds.push_back(box(pos(rng), pos(rng), pos(rng), size(rng)));

  Pairs expected;
  for (size_t i = 0; i < bounds.size(); ++i)
    for (size_t k = i + 1; k < bounds.size(); ++k) {
      const Aabb &a = bounds[i];
      const Aabb &b = bounds[k];
      if (a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y &&
          b.min.y <= a.max.y && a.min.z <= b.max.z && b.min.z <= a.max.z)
        expected.emplace_back(i, k);
    }

  EXPECT_EQ(sweepAndPrune(bounds), expected);
}

TEST(BroadPhaseTest, ColliderAabbBoxAndSphere) {
  core::TransformComponent t;
  t.position = core::Vec3(5, 0, 0);

  ColliderComponent b = makeCollider(ColliderType::Box);
  b.transform.scale = core::Vec3(1, 2, 3);
  Aabb ab = computeColliderAabb(b, t);
  EXPECT_FLOAT_EQ(ab.min.x, 4.0f);
  EXPECT_FLOAT_EQ(ab.max.y, 2.0f);
  EXPECT_FLOAT_EQ(ab.max.z, 3.0f);

  ColliderComponent s = makeCollider(ColliderType::Sphere);
  s.transform.scale = core::Vec3(2, 9, 9); // radius is scale.x
  Aabb as = computeColliderAabb(s, t);
  EXPECT_FLOAT_EQ(as.min.x, 3.0f);
  EXPECT_FLOAT_EQ(as.max.y, 2.0f);
  EXPECT_FLOAT_EQ(as.max.z, 2.0f);
}

TEST(BroadPhaseTest, NeverPrunesAPairTheNarrowPhaseAccepts) {
  std::mt19937 rng(99);
  std::uniform_real_distribution<float> pos(-6.0f, 6.0f);
  std::uniform_real_distribution<float> sz(0.3f, 1.5f);

  std::vector<ColliderComponent> colliders;
  std::vector<core::TransformComponent> transforms;
  std::vector<Aabb> bounds;
  for (int i = 0; i < 120; ++i) {
    ColliderComponent c =
        makeCollider(i % 2 ? ColliderType::Box : ColliderType::Sphere);
    c.transform.scale = core::Vec3(sz(rng), sz(rng), sz(rng));
    core::TransformComponent t;
    t.position = core::Vec3(pos(rng), pos(rng), pos(rng));
    colliders.push_back(c);
    transforms.push_back(t);
    bounds.push_back(computeColliderAabb(c, t));
  }

  Pairs candidates = sweepAndPrune(bounds);
  for (size_t i = 0; i < colliders.size(); ++i)
    for (size_t k = i + 1; k < colliders.size(); ++k) {
      CollisionInfo info;
      if (testCollision(colliders[i], transforms[i], colliders[k],
                        transforms[k], info))
        EXPECT_TRUE(std::binary_search(candidates.begin(), candidates.end(),
                                       std::make_pair(i, k)))
            << i << "," << k;
    }
}
