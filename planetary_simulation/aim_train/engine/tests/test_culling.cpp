#include "render_scene_fixture.hpp"

#include "componeng/renderer/culling/bounds.hpp"
#include "gtest/gtest.h"

#include <cmath>

using namespace rscene;

namespace {

constexpr float kEps = 1e-4f;

Aabb unitBox() {
  Aabb b;
  b.min = Vec3(-1, -1, -1);
  b.max = Vec3(1, 1, 1);
  return b;
}

void expectBox(const Aabb &b, Vec3 lo, Vec3 hi) {
  EXPECT_NEAR(b.min.x, lo.x, kEps);
  EXPECT_NEAR(b.min.y, lo.y, kEps);
  EXPECT_NEAR(b.min.z, lo.z, kEps);
  EXPECT_NEAR(b.max.x, hi.x, kEps);
  EXPECT_NEAR(b.max.y, hi.y, kEps);
  EXPECT_NEAR(b.max.z, hi.z, kEps);
}

// Camera at the origin looking down -Z, 60 degree vertical FOV, square.
Mat4 lookDownMinusZ() {
  return perspective(radians(60.0f), 1.0f, 0.1f, 100.0f) *
         lookAt(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0));
}
Mat4 lookDownPlusX() {
  return perspective(radians(60.0f), 1.0f, 0.1f, 100.0f) *
         lookAt(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0));
}

void setCamera(Scene &s, const Mat4 &viewProj) {
  s.world->getComponent<camera::CameraComponent>(s.camera)
      .viewProjectionMatrix = viewProj;
}

uint32_t drawnInstances(Scene &s) {
  uint32_t n = 0;
  for (const auto &d : s.rec().draws) {
    n += d.instanceCount;
  }
  return n;
}

// Two frames: the first resolves names to ids (nothing can be measured yet, so
// everything is drawn); the second is the first with real culling.
void settle(Scene &s) {
  s.frame();
  s.rec().draws.clear();
}

} // namespace

// --- transformAabb ----------------------------------------------------------

TEST(TransformAabb, IdentityLeavesTheBoxAlone) {
  expectBox(transformAabb(unitBox(), Mat4(1.0f)), Vec3(-1, -1, -1),
            Vec3(1, 1, 1));
}

TEST(TransformAabb, TranslationAndScale) {
  TransformComponent t{{10, 0, -5}, {0, 0, 0}, {2, 3, 4}};
  expectBox(transformAabb(unitBox(), t.toMatrix()), Vec3(8, -3, -9),
            Vec3(12, 3, -1));
}

TEST(TransformAabb, NegativeScaleStillGivesMinBelowMax) {
  TransformComponent t{{0, 0, 0}, {0, 0, 0}, {-2, 1, 1}};
  expectBox(transformAabb(unitBox(), t.toMatrix()), Vec3(-2, -1, -1),
            Vec3(2, 1, 1));
}

TEST(TransformAabb, QuarterTurnSwapsTheExtents) {
  Aabb slab;
  slab.min = Vec3(-3, -1, -1);
  slab.max = Vec3(3, 1, 1);
  TransformComponent t{{0, 0, 0}, {0, 0, 90}, {1, 1, 1}}; // about Z
  expectBox(transformAabb(slab, t.toMatrix()), Vec3(-1, -3, -1), Vec3(1, 3, 1));
}

TEST(TransformAabb, FortyFiveDegreesGrowsToTheEnclosingBox) {
  TransformComponent t{{0, 0, 0}, {0, 0, 45}, {1, 1, 1}};
  const float r = std::sqrt(2.0f);
  expectBox(transformAabb(unitBox(), t.toMatrix()), Vec3(-r, -r, -1),
            Vec3(r, r, 1));
}

TEST(TransformAabb, PaddingInflatesEverySide) {
  expectBox(transformAabb(unitBox(), Mat4(1.0f), 0.5f),
            Vec3(-1.5f, -1.5f, -1.5f), Vec3(1.5f, 1.5f, 1.5f));
}

TEST(TransformAabb, MatchesTransformingAllEightCorners) {
  const TransformComponent t{{3, -2, 7}, {20, 70, -110}, {1.5f, 0.5f, 2.0f}};
  const Mat4 m = t.toMatrix();
  Aabb local;
  local.min = Vec3(-1, -2, -0.5f);
  local.max = Vec3(2, 1, 3);

  Vec3 lo(1e9f), hi(-1e9f);
  for (int i = 0; i < 8; ++i) {
    const float x = (i & 1) ? local.max.x : local.min.x;
    const float y = (i & 2) ? local.max.y : local.min.y;
    const float z = (i & 4) ? local.max.z : local.min.z;
    const float w[3] = {m[0][0] * x + m[1][0] * y + m[2][0] * z + m[3][0],
                        m[0][1] * x + m[1][1] * y + m[2][1] * z + m[3][1],
                        m[0][2] * x + m[1][2] * y + m[2][2] * z + m[3][2]};
    lo = Vec3(std::min(lo.x, w[0]), std::min(lo.y, w[1]), std::min(lo.z, w[2]));
    hi = Vec3(std::max(hi.x, w[0]), std::max(hi.y, w[1]), std::max(hi.z, w[2]));
  }
  expectBox(transformAabb(local, m), lo, hi);
}

// --- Mesh bounds ------------------------------------------------------------

TEST(MeshBounds, ComeFromTheUploadedVertices) {
  auto s = makeScene();
  const Mesh &tri = s->assets().getMesh(s->assets().getMeshID("tri"));
  expectBox(tri.bounds(), Vec3(0, 0, 0), Vec3(1, 1, 0));
  const Mesh &quad = s->assets().getMesh(s->assets().getMeshID("quad"));
  expectBox(quad.bounds(), Vec3(0, 0, 0), Vec3(1, 1, 0));
}

// --- CullingSystem ----------------------------------------------------------

TEST(Culling, EverythingIsDrawnOnTheFirstFrameBeforeIdsAreResolved) {
  auto s = makeScene({}, /*withCulling=*/true);
  setCamera(*s, lookDownMinusZ());
  s->spawn("tri", "matA", 0); // placed at z = 0 by spawn(); in front is below
  EntityID farEntity = s->spawn("tri", "matA", 5000);
  (void)farEntity;
  s->frame();
  EXPECT_EQ(drawnInstances(*s), 2u)
      << "ids resolve during the first batching pass; culling starts after";
}

TEST(Culling, OffscreenEntitiesAreSkippedWithoutAnyCollider) {
  auto s = makeScene({}, true);
  setCamera(*s, lookDownMinusZ());

  auto place = [&](float x, float z) {
    EntityID e = s->spawn("tri", "matA");
    s->world->getComponent<TransformComponent>(e).position = Vec3(x, 0, z);
    return e;
  };
  EntityID ahead = place(0, -10);
  EntityID side = place(500, -10);
  EntityID behind = place(0, 10);

  settle(*s);
  s->frame();

  EXPECT_FALSE(s->world->getComponent<MeshComponent>(ahead).culled);
  EXPECT_TRUE(s->world->getComponent<MeshComponent>(side).culled);
  EXPECT_TRUE(s->world->getComponent<MeshComponent>(behind).culled);
  EXPECT_EQ(drawnInstances(*s), 1u);
}

TEST(Culling, CullingNeverTouchesTheApplicationsVisibleFlag) {
  auto s = makeScene({}, true);
  setCamera(*s, lookDownMinusZ());

  EntityID hidden = s->spawn("tri", "matA");
  s->world->getComponent<TransformComponent>(hidden).position = Vec3(0, 0, -10);
  s->world->getComponent<MeshComponent>(hidden).visible = false;

  EntityID culledButVisible = s->spawn("tri", "matA");
  s->world->getComponent<TransformComponent>(culledButVisible).position =
      Vec3(500, 0, -10);

  settle(*s);
  s->frame();
  s->frame();

  // In view but hidden by the app: stays hidden, and is not drawn.
  EXPECT_FALSE(s->world->getComponent<MeshComponent>(hidden).visible);
  EXPECT_FALSE(s->world->getComponent<MeshComponent>(hidden).culled);
  // Out of view: culled, but the app's flag still says visible.
  EXPECT_TRUE(s->world->getComponent<MeshComponent>(culledButVisible).visible);
  EXPECT_TRUE(s->world->getComponent<MeshComponent>(culledButVisible).culled);
}

TEST(Culling, EntitiesComeBackWhenTheCameraTurnsTowardsThem) {
  auto s = makeScene({}, true);
  setCamera(*s, lookDownMinusZ());

  EntityID east = s->spawn("tri", "matA");
  s->world->getComponent<TransformComponent>(east).position = Vec3(20, 0, 0);

  settle(*s);
  s->frame();
  EXPECT_EQ(drawnInstances(*s), 0u) << "to the side of a -Z camera";

  s->rec().draws.clear();
  setCamera(*s, lookDownPlusX());
  s->frame();
  EXPECT_EQ(drawnInstances(*s), 1u) << "now straight ahead";
  EXPECT_FALSE(s->world->getComponent<MeshComponent>(east).culled);
}

TEST(Culling, MovingAnEntityIntoViewMakesItDrawn) {
  auto s = makeScene({}, true);
  setCamera(*s, lookDownMinusZ());
  EntityID e = s->spawn("tri", "matA");
  s->world->getComponent<TransformComponent>(e).position = Vec3(500, 0, -10);

  settle(*s);
  s->frame();
  EXPECT_EQ(drawnInstances(*s), 0u);

  s->rec().draws.clear();
  s->world->getComponent<TransformComponent>(e).position = Vec3(0, 0, -10);
  s->frame();
  EXPECT_EQ(drawnInstances(*s), 1u);
}

TEST(Culling, ALargeEntityCoveringTheViewIsNotCulled) {
  auto s = makeScene({}, true);
  setCamera(*s, lookDownMinusZ());
  // The fixture's quad is flat; scaled to 1 km and centred on the view axis 10
  // m ahead it fills the screen, though none of its corners are in the frustum.
  EntityID wall = s->spawn("quad", "matA");
  auto &t = s->world->getComponent<TransformComponent>(wall);
  t.position = Vec3(-500, -500, -10);
  t.scale = Vec3(1000, 1000, 1000);

  settle(*s);
  s->frame();
  EXPECT_EQ(drawnInstances(*s), 1u);
}

TEST(Culling, AFlatEntityBeyondTheFarPlaneIsCulled) {
  auto s = makeScene({}, true);
  setCamera(*s, lookDownMinusZ());
  EntityID e = s->spawn("quad", "matA");
  s->world->getComponent<TransformComponent>(e).position = Vec3(0, 0, -500);

  settle(*s);
  s->frame();
  EXPECT_EQ(drawnInstances(*s), 0u);
}

TEST(Culling, MaterialPaddingKeepsShaderMovedGeometryAlive) {
  // At 10 m the half-width of this frustum is 10 * tan(30 deg) = 5.77 m, so a
  // 1 m wide box at x = 6.5 is just outside it.
  auto edge = [](float padding) {
    auto s = makeScene({}, true);
    s->assets()
        .getMaterial(s->assets().getMaterialID("matA"))
        .setBoundsPadding(padding);
    setCamera(*s, lookDownMinusZ());
    EntityID e = s->spawn("tri", "matA");
    s->world->getComponent<TransformComponent>(e).position = Vec3(6.5f, 0, -10);
    settle(*s);
    s->frame();
    return drawnInstances(*s);
  };
  EXPECT_EQ(edge(0.0f), 0u);
  EXPECT_EQ(edge(2.0f), 1u);
}
