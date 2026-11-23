#include "render_scene_fixture.hpp"

#include "componeng/renderer/lod/lod_component.hpp"
#include "componeng/renderer/lod/lod_system.hpp"
#include "componeng/resources/render_settings.hpp"
#include "gtest/gtest.h"

using namespace rscene;

namespace {

// The fixture's camera sits at (1, 2, 3); entities are placed along +X from it
// so their distance is exactly `d`.
constexpr float kCamX = 1.0f, kCamY = 2.0f, kCamZ = 3.0f;

struct LodScene {
  std::unique_ptr<Scene> scene = makeScene();
  HandleID tri = 0, quad = 0;

  LodScene() {
    scene->world->registerComponent<LodComponent>();
    tri = scene->assets().getMeshID("tri");
    quad = scene->assets().getMeshID("quad");
  }

  EntityID spawn(float d, LodComponent lod) {
    EntityID e = scene->world->createEntity();
    scene->world->addComponents(
        e,
        TransformComponent{.position = Vec3(kCamX + d, kCamY, kCamZ),
                           .rotation = Vec3(0.0f),
                           .scale = Vec3(1.0f)},
        MeshComponent{.meshName = "tri", .visible = true},
        MaterialComponent{.materialName = "matA"}, LodComponent(lod));
    return e;
  }

  void run(float dt = 0.016f) {
    LodSystem system;
    system.onUpdate(SystemState{scene->world.get(), dt});
  }

  resources::RenderSettings &settings() {
    return scene->world->getResource<resources::RenderSettings>();
  }

  MeshComponent &mesh(EntityID e) {
    return scene->world->getComponent<MeshComponent>(e);
  }

  void moveCamera(float x) {
    scene->world->getComponent<TransformComponent>(scene->camera).position =
        Vec3(x, kCamY, kCamZ);
  }
};

LodComponent twoLevels(HandleID nearMesh, HandleID farMesh) {
  LodComponent lod;
  lod.mesh[0] = nearMesh;
  lod.mesh[1] = farMesh;
  lod.distance[0] = 10.0f;
  lod.distance[1] = 50.0f;
  return lod;
}

} // namespace

TEST(Lod, ChoosesTheLevelWhoseRangeCoversTheDistance) {
  LodScene s;
  EntityID close = s.spawn(5.0f, twoLevels(s.tri, s.quad));
  EntityID mid = s.spawn(30.0f, twoLevels(s.tri, s.quad));
  s.run();

  EXPECT_TRUE(s.mesh(close).visible);
  EXPECT_EQ(s.mesh(close).meshID, s.tri);
  EXPECT_TRUE(s.mesh(mid).visible);
  EXPECT_EQ(s.mesh(mid).meshID, s.quad);
}

TEST(Lod, HidesTheEntityBeyondTheLastLevel) {
  LodScene s;
  EntityID farEntity = s.spawn(80.0f, twoLevels(s.tri, s.quad));
  s.run();
  EXPECT_FALSE(s.mesh(farEntity).visible);
}

TEST(Lod, SkipsLevelsThatHaveNoMesh) {
  LodScene s;
  // Level 0 absent: a part that only exists at medium range.
  EntityID e = s.spawn(5.0f, twoLevels(0, s.quad));
  s.run();
  EXPECT_TRUE(s.mesh(e).visible);
  EXPECT_EQ(s.mesh(e).meshID, s.quad);
}

TEST(Lod, ShowsAgainWhenTheCameraComesBack) {
  LodScene s;
  EntityID e = s.spawn(80.0f, twoLevels(s.tri, s.quad));
  s.run();
  ASSERT_FALSE(s.mesh(e).visible);

  s.moveCamera(kCamX + 75.0f); // now 5 m away
  s.run();
  EXPECT_TRUE(s.mesh(e).visible);
  EXPECT_EQ(s.mesh(e).meshID, s.tri);
}

TEST(Lod, NeverTouchesEntitiesWithoutALodComponent) {
  LodScene s;
  EntityID plain = s.scene->spawn("quad", "matA", 500.0f);
  s.run();
  EXPECT_TRUE(s.mesh(plain).visible);
}

// --- Screen-height mode ------------------------------------------------------

namespace {

constexpr float kFixtureFov = 60.0f; // CameraComponent's default

// size 2 at scale 1, switching to level 1 at 20 m and hiding at 60 m.
LodComponent screenLevels(const LodScene &s, float size = 2.0f) {
  LodComponent lod;
  lod.mesh[0] = s.tri;
  lod.mesh[1] = s.quad;
  lod.size = size;
  lod.screenHeight[0] = lodScreenHeight(2.0f, 20.0f, kFixtureFov);
  lod.screenHeight[1] = lodScreenHeight(2.0f, 60.0f, kFixtureFov);
  return lod;
}

} // namespace

TEST(LodScreenHeight, ThresholdIsTheCoverageAtThatDistance) {
  // A 2 m object 10 m away with a 60 degree FOV covers 2 / (2 * 10 * tan 30).
  EXPECT_NEAR(lodScreenHeight(2.0f, 10.0f, 60.0f), 0.173205f, 1e-5f);
}

TEST(LodScreenHeight, SwitchesAtTheDistanceTheThresholdWasBuiltFrom) {
  LodScene s;
  EntityID before = s.spawn(19.0f, screenLevels(s));
  EntityID after = s.spawn(21.0f, screenLevels(s));
  EntityID gone = s.spawn(61.0f, screenLevels(s));
  s.run();

  EXPECT_EQ(s.mesh(before).meshID, s.tri);
  EXPECT_EQ(s.mesh(after).meshID, s.quad);
  EXPECT_TRUE(s.mesh(after).visible);
  EXPECT_FALSE(s.mesh(gone).visible);
}

TEST(LodScreenHeight, BiggerObjectsHoldTheirDetailFurther) {
  LodScene s;
  EntityID small = s.spawn(30.0f, screenLevels(s));
  EntityID big = s.spawn(30.0f, screenLevels(s));
  s.scene->world->getComponent<TransformComponent>(big).scale = Vec3(2.0f);
  s.run();

  EXPECT_EQ(s.mesh(small).meshID, s.quad) << "past its 20 m switch";
  EXPECT_EQ(s.mesh(big).meshID, s.tri) << "twice the size: switches at 40 m";
}

TEST(LodScreenHeight, ADeeperZoomKeepsDetailFurther) {
  LodScene s;
  EntityID e = s.spawn(30.0f, screenLevels(s));
  s.run();
  ASSERT_EQ(s.mesh(e).meshID, s.quad);

  s.scene->world->getComponent<camera::CameraComponent>(s.scene->camera).fov =
      30.0f; // narrower view: the same object fills more of the screen
  s.scene->world->getComponent<LodComponent>(e).level = -1;
  s.run();
  EXPECT_EQ(s.mesh(e).meshID, s.tri);
}

TEST(LodScreenHeight, AZeroThresholdKeepsTheLastLevelAtAnyDistance) {
  LodScene s;
  LodComponent lod = screenLevels(s);
  lod.screenHeight[1] = 0.0f;
  EntityID farEntity = s.spawn(5000.0f, lod);
  s.run();
  EXPECT_TRUE(s.mesh(farEntity).visible);
  EXPECT_EQ(s.mesh(farEntity).meshID, s.quad);
}

TEST(LodScreenHeight, ZeroSizeStaysInDistanceMode) {
  LodScene s;
  LodComponent lod = twoLevels(s.tri, s.quad); // size 0
  EntityID e = s.spawn(30.0f, lod);
  s.run();
  EXPECT_EQ(s.mesh(e).meshID, s.quad);
}

// Cross-fade

namespace {

// Fade length the tests run with; a step of kFadeStep takes a quarter of it.
constexpr float kFadeSeconds = 0.4f;
constexpr float kFadeStep = kFadeSeconds / 4.0f;

struct FadeScene : LodScene {
  FadeScene() {
    settings().lodDither = true;
    settings().lodFadeSeconds = kFadeSeconds;
  }

  // An entity settled on level 0 (tri) at 5 m, then moved to `d` metres.
  EntityID settledNearThenMoveTo(float d) {
    EntityID e = spawn(5.0f, twoLevels(tri, quad));
    run();
    scene->world->getComponent<TransformComponent>(e).position.x = kCamX + d;
    return e;
  }
};

} // namespace

TEST(LodFade, TheFirstFrameSettlesWithoutFading) {
  FadeScene s;
  EntityID e = s.spawn(30.0f, twoLevels(s.tri, s.quad));
  s.run();
  EXPECT_EQ(s.mesh(e).meshID, s.quad);
  EXPECT_EQ(s.mesh(e).fade, 0.0f);
  EXPECT_EQ(s.mesh(e).fadeMeshID, 0u);
}

TEST(LodFade, ALevelChangeDithersTheNewMeshInBesideTheOld) {
  FadeScene s;
  EntityID e = s.settledNearThenMoveTo(30.0f);
  s.run(kFadeStep);

  const MeshComponent &m = s.mesh(e);
  EXPECT_TRUE(m.visible);
  EXPECT_EQ(m.meshID, s.quad) << "the incoming mesh is the primary one";
  EXPECT_EQ(m.fadeMeshID, s.tri) << "the old mesh is drawn beside it";
  EXPECT_NEAR(m.fade, -0.75f, 1e-5f) << "a quarter of the way in";
}

TEST(LodFade, TheFadeProgressesWithTimeAndThenSettles) {
  FadeScene s;
  EntityID e = s.settledNearThenMoveTo(30.0f);

  s.run(kFadeStep);
  EXPECT_NEAR(s.mesh(e).fade, -0.75f, 1e-5f);
  s.run(kFadeStep);
  EXPECT_NEAR(s.mesh(e).fade, -0.5f, 1e-5f);
  s.run(kFadeStep);
  s.run(kFadeStep);

  EXPECT_EQ(s.mesh(e).fade, 0.0f);
  EXPECT_EQ(s.mesh(e).fadeMeshID, 0u);
  EXPECT_EQ(s.mesh(e).meshID, s.quad);
  EXPECT_TRUE(s.mesh(e).visible);
}

TEST(LodFade, FadingToNothingDithersTheOldMeshOutThenHides) {
  FadeScene s;
  EntityID e = s.settledNearThenMoveTo(80.0f); // beyond every level
  // Level 0 -> hidden: both levels are skipped, so the old mesh is tri.
  s.run(kFadeStep);

  EXPECT_TRUE(s.mesh(e).visible) << "still on screen while it dissolves";
  EXPECT_EQ(s.mesh(e).meshID, s.tri);
  EXPECT_EQ(s.mesh(e).fadeMeshID, 0u);
  EXPECT_NEAR(s.mesh(e).fade, 0.25f, 1e-5f);

  for (int i = 0; i < 3; ++i) {
    s.run(kFadeStep);
  }
  EXPECT_FALSE(s.mesh(e).visible);
  EXPECT_EQ(s.mesh(e).fade, 0.0f);
}

TEST(LodFade, FadingInFromNothingDithersTheNewMeshIn) {
  FadeScene s;
  EntityID e = s.spawn(80.0f, twoLevels(s.tri, s.quad));
  s.run();
  ASSERT_FALSE(s.mesh(e).visible);

  s.moveCamera(kCamX + 75.0f); // now 5 m away
  s.run(kFadeStep);
  EXPECT_TRUE(s.mesh(e).visible);
  EXPECT_EQ(s.mesh(e).meshID, s.tri);
  EXPECT_EQ(s.mesh(e).fadeMeshID, 0u) << "nothing to dither out";
  EXPECT_NEAR(s.mesh(e).fade, -0.75f, 1e-5f);
}

TEST(LodFade, GoingBackToTheLevelBeingFadedOutReversesTheFade) {
  FadeScene s;
  EntityID e = s.settledNearThenMoveTo(30.0f);
  s.run(kFadeStep);
  s.run(kFadeStep); // halfway to the far mesh
  ASSERT_NEAR(s.mesh(e).fade, -0.5f, 1e-5f);

  s.scene->world->getComponent<TransformComponent>(e).position.x = kCamX + 5.0f;
  s.run(0.0f);

  // Coverage carries over: the near mesh is back, half in.
  EXPECT_EQ(s.mesh(e).meshID, s.tri);
  EXPECT_EQ(s.mesh(e).fadeMeshID, s.quad);
  EXPECT_NEAR(s.mesh(e).fade, -0.5f, 1e-5f);

  s.run(kFadeStep * 2.0f);
  EXPECT_EQ(s.mesh(e).fade, 0.0f);
  EXPECT_EQ(s.mesh(e).meshID, s.tri);
  EXPECT_EQ(s.mesh(e).fadeMeshID, 0u);
}

TEST(LodFade, ATransitionDoesNotRestartWhileTheLevelHolds) {
  FadeScene s;
  EntityID e = s.settledNearThenMoveTo(30.0f);
  s.run(kFadeStep);
  s.run(kFadeStep);
  EXPECT_NEAR(s.mesh(e).fade, -0.5f, 1e-5f) << "kept advancing, not restarted";
}

TEST(LodFade, SwitchesAtOnceWhenDitheringIsOff) {
  FadeScene s;
  s.settings().lodDither = false;
  EntityID e = s.settledNearThenMoveTo(30.0f);
  s.run(kFadeStep);

  EXPECT_EQ(s.mesh(e).meshID, s.quad);
  EXPECT_EQ(s.mesh(e).fade, 0.0f);
  EXPECT_EQ(s.mesh(e).fadeMeshID, 0u);
}

TEST(LodFade, SwitchesAtOnceWithAZeroFadeTime) {
  FadeScene s;
  s.settings().lodFadeSeconds = 0.0f;
  EntityID e = s.settledNearThenMoveTo(80.0f);
  s.run(kFadeStep);
  EXPECT_FALSE(s.mesh(e).visible);
  EXPECT_EQ(s.mesh(e).fade, 0.0f);
}

TEST(LodFade, TurningDitheringOffFinishesATransitionInProgress) {
  FadeScene s;
  EntityID e = s.settledNearThenMoveTo(30.0f);
  s.run(kFadeStep);
  ASSERT_NE(s.mesh(e).fade, 0.0f);

  s.settings().lodDither = false;
  s.run(kFadeStep);
  EXPECT_EQ(s.mesh(e).fade, 0.0f);
  EXPECT_EQ(s.mesh(e).fadeMeshID, 0u);
  EXPECT_EQ(s.mesh(e).meshID, s.quad);
}

TEST(LodFade, TwoLevelsSharingAMeshHaveNothingToFade) {
  FadeScene s;
  EntityID e = s.settledNearThenMoveTo(30.0f);
  s.scene->world->getComponent<LodComponent>(e).mesh[1] = s.tri;
  s.run(kFadeStep);
  EXPECT_EQ(s.mesh(e).fade, 0.0f);
  EXPECT_EQ(s.mesh(e).meshID, s.tri);
}
