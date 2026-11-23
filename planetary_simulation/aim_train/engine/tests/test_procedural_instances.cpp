#include "render_scene_fixture.hpp"

#include "componeng/renderer/component/procedural_instances_component.hpp"
#include "gtest/gtest.h"

using namespace rscene;

namespace {

// A shader with no per-instance attributes, as a procedural material has.
std::unique_ptr<Scene> makeProceduralScene(bool withCulling = false) {
  return makeScene([](fake::Recorder &r) { r.instanceLayout = {}; },
                   withCulling);
}

EntityID spawnProcedural(Scene &s, int count, bool cull = false,
                         const char *mesh = "tri",
                         const char *material = "matA", float x = 0.0f) {
  EntityID e = s.spawn(mesh, material, x);
  s.world->addComponents(
      e, ProceduralInstancesComponent{.count = count, .cull = cull});
  return e;
}

Mat4 lookDownMinusZ() {
  return perspective(radians(60.0f), 1.0f, 0.1f, 100.0f) *
         lookAt(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0));
}

void setCamera(Scene &s, const Mat4 &viewProj) {
  s.world->getComponent<camera::CameraComponent>(s.camera)
      .viewProjectionMatrix = viewProj;
}

// The first frame resolves names to ids and draws everything; measure the next.
void settle(Scene &s) {
  s.frame();
  s.rec().draws.clear();
}

} // namespace

TEST(ProceduralInstances, OneEntityDrivesOneDrawOfCountInstances) {
  auto s = makeProceduralScene();
  spawnProcedural(*s, 5000);
  s->frame();

  ASSERT_EQ(s->rec().draws.size(), 1u);
  EXPECT_EQ(s->rec().draws[0].instanceCount, 5000u);
  EXPECT_EQ(s->rec().draws[0].indexCount, 3u);
}

TEST(ProceduralInstances, UploadsNoInstanceData) {
  auto s = makeProceduralScene();
  spawnProcedural(*s, 100);
  s->frame();

  ASSERT_EQ(s->rec().draws.size(), 1u);
  EXPECT_TRUE(s->rec().draws[0].instanceFloats.empty());
  // The per-frame values still reach the shader: only instance data is absent.
  EXPECT_FALSE(s->rec().uniformBufferBinds.empty());
}

TEST(ProceduralInstances, EachEntityIsItsOwnDraw) {
  auto s = makeProceduralScene();
  spawnProcedural(*s, 10, false, "tri", "matA", 0.0f);
  spawnProcedural(*s, 20, false, "tri", "matA", 1.0f);
  s->frame();

  ASSERT_EQ(s->rec().draws.size(), 2u);
  EXPECT_EQ(s->rec().draws[0].instanceCount + s->rec().draws[1].instanceCount,
            30u);
}

TEST(ProceduralInstances, ZeroCountAndHiddenEntitiesDrawNothing) {
  auto s = makeProceduralScene();
  spawnProcedural(*s, 0);
  EntityID hidden = spawnProcedural(*s, 50);
  s->world->getComponent<MeshComponent>(hidden).visible = false;
  s->frame();

  EXPECT_TRUE(s->rec().draws.empty());
}

TEST(ProceduralInstances, OrdinaryEntitiesStillBatchAlongsideThem) {
  // Ordinary entities use the fixture's instance layout; the procedural one
  // shares that shader here, which only costs an error log line.
  auto s = makeScene();
  s->spawn("tri", "matB", 0);
  s->spawn("tri", "matB", 1);
  s->spawn("quad", "matA", 2);
  spawnProcedural(*s, 7, false, "tri", "matC", 3);
  s->frame();

  uint32_t ordinary = 0, procedural = 0;
  for (const auto &d : s->rec().draws) {
    if (d.instanceFloats.empty()) {
      procedural += d.instanceCount;
    } else {
      ordinary += d.instanceCount;
    }
  }
  EXPECT_EQ(procedural, 7u);
  EXPECT_EQ(ordinary, 3u);
  EXPECT_EQ(s->rec().draws.size(),
            3u); // (tri,matB) x2, (quad,matA), procedural
}

TEST(ProceduralInstances, IsNotCulledByDefaultEvenFarOutsideTheFrustum) {
  auto s = makeProceduralScene(true);
  setCamera(*s, lookDownMinusZ());
  // 500 m to the side of a camera looking down -Z: well outside the frustum.
  spawnProcedural(*s, 10, false, "tri", "matA", 500.0f);
  settle(*s);
  s->frame();

  ASSERT_EQ(s->rec().draws.size(), 1u);
  EXPECT_EQ(s->rec().draws[0].instanceCount, 10u);
}

TEST(ProceduralInstances, CullsLikeAnyEntityWhenAskedTo) {
  auto s = makeProceduralScene(true);
  setCamera(*s, lookDownMinusZ());
  spawnProcedural(*s, 10, true, "tri", "matA", 500.0f);
  EntityID inView = spawnProcedural(*s, 20, true, "tri", "matA", 0.0f);
  s->world->getComponent<TransformComponent>(inView).position = Vec3(0, 0, -10);
  settle(*s);
  s->frame();

  ASSERT_EQ(s->rec().draws.size(), 1u);
  EXPECT_EQ(s->rec().draws[0].instanceCount, 20u);
}
