// #include "componeng/components/collider_component.hpp"
// #include "componeng/core/transform_component.hpp"
// #include "componeng/physics/collision_detection.hpp"
// #include <gtest/gtest.h>
//
// static ColliderComponent makeAABB(Vec3 localCenter, Vec3 halfExtents) {
//   return ColliderComponent{
//       .type = ColliderType::AABB,
//       .shape = AABB{.localCenter = localCenter, .halfExtents = halfExtents}};
// }
//
// static ColliderComponent makeSphere(Vec3 localCenter, float radius) {
//   return ColliderComponent{
//       .type = ColliderType::Sphere,
//       .shape = Sphere{.localCenter = localCenter, .radius = radius}};
// }
//
// TEST(CollisionTest, AABBvsAABBOverlapping) {
//   ColliderComponent a = makeAABB(Vec3(0.0f), Vec3(1.0f));
//   ColliderComponent b = makeAABB(Vec3(0.0f), Vec3(1.0f));
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(0.5f, 0.0f, 0.0f)};
//
//   CollisionInfo info;
//   EXPECT_TRUE(testCollision(a, ta, b, tb, info));
//   EXPECT_GT(info.penetration, 0.0f);
// }
//
// TEST(CollisionTest, AABBvsAABBNotOverlapping) {
//   ColliderComponent a = makeAABB(Vec3(0.0f), Vec3(1.0f));
//   ColliderComponent b = makeAABB(Vec3(0.0f), Vec3(1.0f));
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(10.0f, 0.0f, 0.0f)};
//
//   CollisionInfo info;
//   EXPECT_FALSE(testCollision(a, ta, b, tb, info));
// }
//
// TEST(CollisionTest, AABBvsAABBEdgeTouching) {
//   ColliderComponent a = makeAABB(Vec3(0.0f), Vec3(1.0f));
//   ColliderComponent b = makeAABB(Vec3(0.0f), Vec3(1.0f));
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(2.0f, 0.0f, 0.0f)};
//
//   CollisionInfo info;
//   EXPECT_FALSE(testCollision(a, ta, b, tb, info));
// }
//
// TEST(CollisionTest, SpherevsSphereOverlapping) {
//   ColliderComponent a = makeSphere(Vec3(0.0f), 2.0f);
//   ColliderComponent b = makeSphere(Vec3(0.0f), 2.0f);
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(1.0f, 0.0f, 0.0f)};
//
//   CollisionInfo info;
//   EXPECT_TRUE(testCollision(a, ta, b, tb, info));
//   EXPECT_FLOAT_EQ(info.penetration, 3.0f);
// }
//
// TEST(CollisionTest, SpherevsSphereNotOverlapping) {
//   ColliderComponent a = makeSphere(Vec3(0.0f), 1.0f);
//   ColliderComponent b = makeSphere(Vec3(0.0f), 1.0f);
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(5.0f, 0.0f, 0.0f)};
//
//   CollisionInfo info;
//   EXPECT_FALSE(testCollision(a, ta, b, tb, info));
// }
//
// TEST(CollisionTest, SpherevsSphereTouching) {
//   ColliderComponent a = makeSphere(Vec3(0.0f), 1.0f);
//   ColliderComponent b = makeSphere(Vec3(0.0f), 1.0f);
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(2.0f, 0.0f, 0.0f)};
//
//   CollisionInfo info;
//   EXPECT_FALSE(testCollision(a, ta, b, tb, info));
// }
//
// TEST(CollisionTest, AABBvsSphereOverlapping) {
//   ColliderComponent a = makeAABB(Vec3(0.0f), Vec3(2.0f));
//   ColliderComponent b = makeSphere(Vec3(0.0f), 1.0f);
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(1.0f, 0.0f, 0.0f)};
//
//   CollisionInfo info;
//   EXPECT_TRUE(testCollision(a, ta, b, tb, info));
// }
//
// TEST(CollisionTest, AABBvsSphereNotOverlapping) {
//   ColliderComponent a = makeAABB(Vec3(0.0f), Vec3(1.0f));
//   ColliderComponent b = makeSphere(Vec3(0.0f), 0.5f);
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(10.0f, 0.0f, 0.0f)};
//
//   CollisionInfo info;
//   EXPECT_FALSE(testCollision(a, ta, b, tb, info));
// }
//
// TEST(CollisionTest, SpherevsAABBOverlapping) {
//   ColliderComponent a = makeSphere(Vec3(0.0f), 2.0f);
//   ColliderComponent b = makeAABB(Vec3(0.0f), Vec3(1.0f));
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(0.5f, 0.0f, 0.0f)};
//
//   CollisionInfo info;
//   EXPECT_TRUE(testCollision(a, ta, b, tb, info));
// }
//
// TEST(CollisionTest, CollisionNormalPointsFromAToB) {
//   ColliderComponent a = makeAABB(Vec3(0.0f), Vec3(1.0f));
//   ColliderComponent b = makeAABB(Vec3(0.0f), Vec3(1.0f));
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(1.5f, 0.0f, 0.0f)};
//
//   CollisionInfo info;
//   testCollision(a, ta, b, tb, info);
//   EXPECT_GT(info.normal.x, 0.0f);
// }
//
// TEST(CollisionTest, SphereInsideAABB) {
//   ColliderComponent a = makeAABB(Vec3(0.0f), Vec3(5.0f));
//   ColliderComponent b = makeSphere(Vec3(0.0f), 1.0f);
//
//   TransformComponent ta{.position = Vec3(0.0f)};
//   TransformComponent tb{.position = Vec3(0.0f)};
//
//   CollisionInfo info;
//   EXPECT_TRUE(testCollision(a, ta, b, tb, info));
// }
//
// TEST(CollisionTest, getColliderWorldPosition) {
//   TransformComponent t{.position = Vec3(10.0f, 5.0f, 3.0f)};
//   Vec3 localCenter(1.0f, 2.0f, 3.0f);
//
//   Vec3 worldPos = getColliderWorldPosition(t, localCenter);
//   EXPECT_FLOAT_EQ(worldPos.x, 11.0f);
//   EXPECT_FLOAT_EQ(worldPos.y, 7.0f);
//   EXPECT_FLOAT_EQ(worldPos.z, 6.0f);
// }
