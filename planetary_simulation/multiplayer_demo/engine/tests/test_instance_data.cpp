#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset/diffuse.hpp"
#include "componeng/renderer/asset/material.hpp"
#include "componeng/renderer/batching/instance_data.hpp"
#include "componeng/renderer/batching/render_queue.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace componeng::core;
using namespace componeng::ecs;
using namespace componeng::renderer;

namespace {

constexpr float kEps = 1e-5f;

// The composition the engine used before TransformComponent::toMatrix().
Mat4 referenceModelMatrix(const TransformComponent &t) {
  Mat4 m(1.0f);
  m = translate(m, t.position);
  m = rotate(m, radians(t.rotation.x), Vec3(1.0f, 0.0f, 0.0f));
  m = rotate(m, radians(t.rotation.y), Vec3(0.0f, 1.0f, 0.0f));
  m = rotate(m, radians(t.rotation.z), Vec3(0.0f, 0.0f, 1.0f));
  m = scale(m, t.scale);
  return m;
}

::testing::AssertionResult SameMat4(const Mat4 &a, const Mat4 &b,
                                    float eps = kEps) {
  float worst = 0.0f;
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      worst = std::max(worst, std::fabs(a[c][r] - b[c][r]));
  if (worst <= eps)
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure() << "worst |a-b| = " << worst;
}

// instanceModel (mat4, 4 rows) + instanceColor (vec4) + wind (float).
api::VertexLayout modelColorWindLayout() {
  api::VertexLayout layout;
  for (int row = 0; row < 4; ++row) {
    layout.attributes.push_back(
        {Name("instanceModel"), uint32_t(row * 16), 4, false, row});
  }
  layout.attributes.push_back({Name("instanceColor"), 64, 4, false, -1});
  layout.attributes.push_back({Name("wind"), 80, 1, false, -1});
  layout.stride = 84;
  return layout;
}

} // namespace

// --- TransformComponent::toMatrix -------------------------------------------

TEST(TransformToMatrix, MatchesComposedTranslateRotateScale) {
  const TransformComponent cases[] = {
      {{0, 0, 0}, {0, 0, 0}, {1, 1, 1}},
      {{1, 2, 3}, {0, 0, 0}, {2, 2, 2}},
      {{0, 0, 0}, {90, 0, 0}, {1, 1, 1}},
      {{0, 0, 0}, {0, 90, 0}, {1, 1, 1}},
      {{0, 0, 0}, {0, 0, 90}, {1, 1, 1}},
      {{1, 2, 3}, {90, 45, 30}, {2, 0.5f, 3}},
      {{-5, 0.5f, 10}, {17, -69, 137}, {1, 1, 1}},
      {{7, -3, 2}, {180, 0, 45}, {0.1f, 10, 1}},
      {{-2, -2, -2}, {-40, 109, -155}, {-1, 3, 0.5f}},
      {{0, 0, 0}, {720, -360, 405}, {1, 1, 1}},
  };
  int i = 0;
  for (const auto &t : cases) {
    EXPECT_TRUE(SameMat4(t.toMatrix(), referenceModelMatrix(t)))
        << "case " << i++;
  }
}

TEST(TransformToMatrix, UnrotatedIsExactlyTranslationTimesScale) {
  const TransformComponent t{{4, 5, 6}, {0, 0, 0}, {2, 3, 4}};
  const Mat4 m = t.toMatrix();
  EXPECT_EQ(m[0][0], 2.0f);
  EXPECT_EQ(m[1][1], 3.0f);
  EXPECT_EQ(m[2][2], 4.0f);
  EXPECT_EQ(m[3][0], 4.0f);
  EXPECT_EQ(m[3][1], 5.0f);
  EXPECT_EQ(m[3][2], 6.0f);
  EXPECT_EQ(m[3][3], 1.0f);
  EXPECT_EQ(m[0][1], 0.0f);
  EXPECT_EQ(m[1][0], 0.0f);
}

// --- InstanceWriter ---------------------------------------------------------

TEST(InstanceWriter, WritesEachAttributeAtItsOffset) {
  const api::VertexLayout layout = modelColorWindLayout();
  std::vector<float> data(layout.stride / sizeof(float), -1.0f);
  InstanceWriter w(layout, data.data());

  Mat4 model(1.0f);
  model[3][0] = 9.0f;
  model[0][0] = 2.0f;
  w.set(Name("instanceModel"), model);
  w.set(Name("instanceColor"), Vec4(0.1f, 0.2f, 0.3f, 0.4f));
  w.set(Name("wind"), 0.75f);

  // Slot r holds column r of the matrix, i.e. 16 floats == value_ptr order.
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(data[i], value_ptr(model)[i]) << i;
  }
  EXPECT_EQ(data[16], 0.1f);
  EXPECT_EQ(data[17], 0.2f);
  EXPECT_EQ(data[18], 0.3f);
  EXPECT_EQ(data[19], 0.4f);
  EXPECT_EQ(data[20], 0.75f);
}

TEST(InstanceWriter, IgnoresNamesTheShaderDoesNotDeclare) {
  const api::VertexLayout layout = modelColorWindLayout();
  std::vector<float> data(layout.stride / sizeof(float), 0.0f);
  InstanceWriter w(layout, data.data());

  w.set(Name("notAnAttribute"), Vec3(1, 2, 3));
  w.set(Name("notAnAttribute"), Mat4(1.0f));

  EXPECT_TRUE(
      std::all_of(data.begin(), data.end(), [](float f) { return f == 0.0f; }));
}

TEST(InstanceWriter, TruncatesValuesWiderThanTheAttribute) {
  // "a" is a vec3 followed directly by "b"; writing a Vec4 into "a" must not
  // clobber "b".
  api::VertexLayout layout;
  layout.attributes.push_back({Name("a"), 0, 3, false, -1});
  layout.attributes.push_back({Name("b"), 12, 1, false, -1});
  layout.stride = 16;

  std::vector<float> data(4, 0.0f);
  InstanceWriter w(layout, data.data());
  w.set(Name("b"), 5.0f);
  w.set(Name("a"), Vec4(1, 2, 3, 99));

  EXPECT_EQ(data[0], 1.0f);
  EXPECT_EQ(data[1], 2.0f);
  EXPECT_EQ(data[2], 3.0f);
  EXPECT_EQ(data[3], 5.0f);
}

TEST(InstanceWriter, LeavesNarrowerValuesTailUntouched) {
  api::VertexLayout layout;
  layout.attributes.push_back({Name("c"), 0, 4, false, -1});
  layout.stride = 16;

  std::vector<float> data(4, 7.0f);
  InstanceWriter w(layout, data.data());
  w.set(Name("c"), Vec2(1, 2));

  EXPECT_EQ(data[0], 1.0f);
  EXPECT_EQ(data[1], 2.0f);
  EXPECT_EQ(data[2], 7.0f);
  EXPECT_EQ(data[3], 7.0f);
}

// --- Materials --------------------------------------------------------------

namespace {

std::unique_ptr<World> makeWorld() {
  auto world = std::make_unique<World>();
  world->registerComponents<TransformComponent, ColorComponent>();
  return world;
}

} // namespace

TEST(MaterialInstanceData, DiffuseWritesModelAndColor) {
  auto world = makeWorld();
  EntityID e = world->createEntity();
  TransformComponent t{{1, 2, 3}, {0, 30, 0}, {2, 2, 2}};
  world->addComponents(e, TransformComponent(t),
                       ColorComponent{.color = Vec4(0.1f, 0.2f, 0.3f, 1.0f)});

  DiffuseMaterial material(1, 1);
  const api::VertexLayout layout = modelColorWindLayout();
  material.setVertexLayout(layout);

  std::vector<float> data(layout.stride / sizeof(float), 0.0f);
  InstanceWriter writer(layout, data.data());
  const Mat4 model = world->getComponent<TransformComponent>(e).toMatrix();
  InstanceContext ctx(*world, e, world->getComponent<TransformComponent>(e),
                      model);
  material.writeInstanceData(ctx, writer);

  const Mat4 expected = referenceModelMatrix(t);
  for (int i = 0; i < 16; ++i) {
    EXPECT_NEAR(data[i], value_ptr(expected)[i], kEps) << i;
  }
  EXPECT_EQ(data[16], 0.1f);
  EXPECT_EQ(data[17], 0.2f);
  EXPECT_EQ(data[18], 0.3f);
}

TEST(MaterialInstanceData, DiffuseDefaultsColorToWhiteWithoutComponent) {
  auto world = makeWorld();
  EntityID e = world->createEntity();
  world->addComponents(e, TransformComponent{});

  DiffuseMaterial material(1, 1);
  const api::VertexLayout layout = modelColorWindLayout();
  material.setVertexLayout(layout);

  std::vector<float> data(layout.stride / sizeof(float), 0.0f);
  InstanceWriter writer(layout, data.data());
  const Mat4 model = world->getComponent<TransformComponent>(e).toMatrix();
  InstanceContext ctx(*world, e, world->getComponent<TransformComponent>(e),
                      model);
  material.writeInstanceData(ctx, writer);

  for (int i = 16; i < 20; ++i) {
    EXPECT_EQ(data[i], 1.0f) << i;
  }
}

// --- World::tryGetComponent -------------------------------------------------

TEST(WorldTryGetComponent, ReturnsPointerOrNull) {
  auto world = makeWorld();
  EntityID e = world->createEntity();
  world->addComponents(e, TransformComponent{{1, 2, 3}, {}, {1, 1, 1}});

  auto *t = world->tryGetComponent<TransformComponent>(e);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->position.y, 2.0f);
  EXPECT_EQ(world->tryGetComponent<ColorComponent>(e), nullptr);
}

// --- RenderQueue ------------------------------------------------------------

TEST(RenderQueue, SortsByShaderThenTextureThenMaterialThenMesh) {
  RenderQueue q;
  //            mesh material shader texture
  q.openBatch(2, 10, 2, 5);
  q.openBatch(1, 11, 1, 9);
  q.openBatch(3, 12, 1, 4);
  q.openBatch(1, 13, 1, 4);
  q.openBatch(2, 13, 1, 4);
  q.sort();

  ASSERT_EQ(q.size(), 5u);
  // shader 1 first; within it texture 4 before 9; within texture 4 material 12
  // before 13; within material 13 mesh 1 before 2. Shader 2 last.
  EXPECT_EQ(q.sorted(0).materialID, 12u);
  EXPECT_EQ(q.sorted(1).materialID, 13u);
  EXPECT_EQ(q.sorted(1).meshID, 1u);
  EXPECT_EQ(q.sorted(2).materialID, 13u);
  EXPECT_EQ(q.sorted(2).meshID, 2u);
  EXPECT_EQ(q.sorted(3).textureID, 9u);
  EXPECT_EQ(q.sorted(4).shaderID, 2u);
}

TEST(RenderQueue, OnlyOpenedBatchesExist) {
  RenderQueue q;
  q.openBatch(1, 1, 1, 1);
  q.sort();
  EXPECT_EQ(q.size(), 1u);

  q.clear();
  q.sort();
  EXPECT_EQ(q.size(), 0u) << "a cleared queue must not replay old batches";
}

TEST(RenderQueue, ClearKeepsStorageForReuse) {
  RenderQueue q;
  uint32_t first = q.openBatch(1, 1, 1, 1);
  RenderBatch &b = q.batch(first);
  b.instanceData.assign(1000, 1.0f);
  b.instanceCount = 10;
  const float *storage = b.instanceData.data();

  q.clear();
  uint32_t again = q.openBatch(2, 3, 4, 5);
  RenderBatch &reused = q.batch(again);

  EXPECT_EQ(reused.meshID, 2u);
  EXPECT_EQ(reused.instanceCount, 0u);
  EXPECT_TRUE(reused.instanceData.empty());
  EXPECT_GE(reused.instanceData.capacity(), 1000u);
  reused.instanceData.resize(1000);
  EXPECT_EQ(reused.instanceData.data(), storage)
      << "refilling to the previous size must not reallocate";
}

// --- Slots: names resolved once, writes without searching
// ---------------------

TEST(InstanceSlots, ResolveOffsetsAndWidthsFromTheLayout) {
  const api::VertexLayout layout = modelColorWindLayout();

  const InstanceSlot color = findInstanceSlot(layout, Name("instanceColor"));
  EXPECT_TRUE(color.valid());
  EXPECT_EQ(color.offset, 16u) << "64 bytes in, expressed in floats";
  EXPECT_EQ(color.components, 4u);

  const InstanceSlot wind = findInstanceSlot(layout, Name("wind"));
  EXPECT_EQ(wind.offset, 20u);
  EXPECT_EQ(wind.components, 1u);

  EXPECT_FALSE(findInstanceSlot(layout, Name("notThere")).valid());
}

TEST(InstanceSlots, MatrixAndScalarLookupsDoNotCrossOver) {
  const api::VertexLayout layout = modelColorWindLayout();

  const InstanceMatrixSlot model =
      findInstanceMatrixSlot(layout, Name("instanceModel"));
  ASSERT_TRUE(model.valid());
  EXPECT_EQ(model.rowOffset[0], 0u);
  EXPECT_EQ(model.rowOffset[1], 4u);
  EXPECT_EQ(model.rowOffset[2], 8u);
  EXPECT_EQ(model.rowOffset[3], 12u);

  // A mat4 attribute is not a vec4, and a vec4 is not a mat4.
  EXPECT_FALSE(findInstanceSlot(layout, Name("instanceModel")).valid());
  EXPECT_FALSE(findInstanceMatrixSlot(layout, Name("instanceColor")).valid());
  EXPECT_FALSE(findInstanceMatrixSlot(layout, Name("notThere")).valid());
}

TEST(InstanceSlots, AMatrixMissingARowIsInvalid) {
  api::VertexLayout layout;
  for (int row = 0; row < 3; ++row) { // only three of four rows
    layout.attributes.push_back({Name("m"), uint32_t(row * 16), 4, false, row});
  }
  layout.stride = 48;
  EXPECT_FALSE(findInstanceMatrixSlot(layout, Name("m")).valid());
}

TEST(InstanceSlots, WritingThroughSlotsEqualsWritingByName) {
  const api::VertexLayout layout = modelColorWindLayout();

  Mat4 model(1.0f);
  model[0][0] = 2.0f;
  model[3][1] = 7.5f;
  const Vec4 color(0.1f, 0.2f, 0.3f, 0.4f);

  std::vector<float> byName(layout.stride / sizeof(float), -1.0f);
  InstanceWriter named(layout, byName.data());
  named.set(Name("instanceModel"), model);
  named.set(Name("instanceColor"), color);
  named.set(Name("wind"), 0.75f);

  std::vector<float> bySlot(layout.stride / sizeof(float), -1.0f);
  InstanceWriter slotted(layout, bySlot.data());
  slotted.set(findInstanceMatrixSlot(layout, Name("instanceModel")), model);
  slotted.set(findInstanceSlot(layout, Name("instanceColor")), color);
  slotted.set(findInstanceSlot(layout, Name("wind")), 0.75f);

  EXPECT_EQ(bySlot, byName);
}

TEST(InstanceSlots, TruncateWideValuesAndLeaveNarrowOnesTailsAlone) {
  api::VertexLayout layout;
  layout.attributes.push_back({Name("a"), 0, 3, false, -1});  // vec3
  layout.attributes.push_back({Name("b"), 12, 1, false, -1}); // float
  layout.attributes.push_back({Name("c"), 16, 4, false, -1}); // vec4
  layout.stride = 32;

  std::vector<float> data(8, 7.0f);
  InstanceWriter w(layout, data.data());
  w.set(findInstanceSlot(layout, Name("b")), 5.0f);
  w.set(findInstanceSlot(layout, Name("a")), Vec4(1, 2, 3, 99)); // too wide
  w.set(findInstanceSlot(layout, Name("c")), Vec2(8, 9));        // too narrow

  EXPECT_EQ(data[0], 1.0f);
  EXPECT_EQ(data[1], 2.0f);
  EXPECT_EQ(data[2], 3.0f);
  EXPECT_EQ(data[3], 5.0f) << "the vec4 must not spill into the next attribute";
  EXPECT_EQ(data[4], 8.0f);
  EXPECT_EQ(data[5], 9.0f);
  EXPECT_EQ(data[6], 7.0f) << "unwritten components keep their value";
  EXPECT_EQ(data[7], 7.0f);
}

TEST(InstanceSlots, WritingAnInvalidSlotChangesNothing) {
  const api::VertexLayout layout = modelColorWindLayout();
  std::vector<float> data(layout.stride / sizeof(float), 3.0f);
  InstanceWriter w(layout, data.data());

  const InstanceSlot missing = findInstanceSlot(layout, Name("notThere"));
  const InstanceMatrixSlot missingMatrix =
      findInstanceMatrixSlot(layout, Name("notThere"));
  w.set(missing, 1.0f);
  w.set(missing, Vec4(1, 2, 3, 4));
  w.set(missingMatrix, Mat4(2.0f));
  w.set(InstanceSlot{}, 9.0f); // a default-constructed slot is invalid too

  EXPECT_TRUE(
      std::all_of(data.begin(), data.end(), [](float f) { return f == 3.0f; }));
}

TEST(InstanceSlots, MatrixRowsNeedNotBeContiguous) {
  // Hand-built layout with the four rows interleaved with other attributes.
  api::VertexLayout layout;
  layout.attributes.push_back({Name("m"), 0, 4, false, 0});
  layout.attributes.push_back({Name("x"), 16, 4, false, -1});
  layout.attributes.push_back({Name("m"), 32, 4, false, 1});
  layout.attributes.push_back({Name("y"), 48, 4, false, -1});
  layout.attributes.push_back({Name("m"), 64, 4, false, 2});
  layout.attributes.push_back({Name("m"), 80, 4, false, 3});
  layout.stride = 96;

  Mat4 m(1.0f);
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      m[c][r] = float(c * 4 + r + 1);

  std::vector<float> data(24, 0.0f);
  InstanceWriter w(layout, data.data());
  w.set(findInstanceMatrixSlot(layout, Name("m")), m);

  for (int r = 0; r < 4; ++r) {         // row slot r holds column r
    const int base[4] = {0, 8, 16, 20}; // float offsets of the four rows
    for (int k = 0; k < 4; ++k) {
      EXPECT_EQ(data[base[r] + k], m[r][k]) << "row " << r << " comp " << k;
    }
  }
  // The attributes in between were not touched.
  for (int k = 0; k < 4; ++k) {
    EXPECT_EQ(data[4 + k], 0.0f);
    EXPECT_EQ(data[12 + k], 0.0f);
  }
}

namespace {
class RecordingMaterial : public IMaterial {
public:
  using IMaterial::IMaterial;
  void resolveInstanceSlots(const api::VertexLayout &layout) override {
    ++resolveCalls;
    lastStride = layout.stride;
  }
  void writeInstanceData(const InstanceContext &,
                         InstanceWriter &) const override {
  }
  int resolveCalls = 0;
  uint32_t lastStride = 0;
};
} // namespace

TEST(InstanceSlots, MaterialsAreToldWheneverTheyReceiveALayout) {
  RecordingMaterial material(1, 1);
  EXPECT_EQ(material.resolveCalls, 0);

  material.setVertexLayout(modelColorWindLayout());
  EXPECT_EQ(material.resolveCalls, 1);
  EXPECT_EQ(material.lastStride, 84u);

  api::VertexLayout other;
  other.stride = 32;
  material.setVertexLayout(other);
  EXPECT_EQ(material.resolveCalls, 2);
  EXPECT_EQ(material.lastStride, 32u);
}

TEST(InstanceSlots, DiffuseWritesToWhereTheCurrentLayoutPutsItsAttributes) {
  auto world = makeWorld();
  EntityID e = world->createEntity();
  TransformComponent t{{4, 5, 6}, {0, 0, 0}, {1, 1, 1}};
  world->addComponents(e, TransformComponent(t),
                       ColorComponent{.color = Vec4(0.1f, 0.2f, 0.3f, 0.4f)});

  // Layout B: colour first, then the matrix, i.e. different offsets from A.
  api::VertexLayout layoutB;
  layoutB.attributes.push_back({Name("instanceColor"), 0, 4, false, -1});
  for (int row = 0; row < 4; ++row) {
    layoutB.attributes.push_back(
        {Name("instanceModel"), uint32_t(16 + row * 16), 4, false, row});
  }
  layoutB.stride = 80;

  DiffuseMaterial material(1, 1);
  const Mat4 model = world->getComponent<TransformComponent>(e).toMatrix();

  // Before any layout arrives the slots are invalid: nothing is written.
  {
    std::vector<float> data(20, -1.0f);
    InstanceWriter writer(layoutB, data.data());
    InstanceContext ctx(*world, e, world->getComponent<TransformComponent>(e),
                        model);
    material.writeInstanceData(ctx, writer);
    EXPECT_TRUE(std::all_of(data.begin(), data.end(),
                            [](float f) { return f == -1.0f; }));
  }

  // Layout A, then layout B: each write follows the layout most recently set.
  material.setVertexLayout(modelColorWindLayout());
  material.setVertexLayout(layoutB);

  std::vector<float> data(20, 0.0f);
  InstanceWriter writer(layoutB, data.data());
  InstanceContext ctx(*world, e, world->getComponent<TransformComponent>(e),
                      model);
  material.writeInstanceData(ctx, writer);

  EXPECT_EQ(data[0], 0.1f);
  EXPECT_EQ(data[1], 0.2f);
  EXPECT_EQ(data[3], 0.4f);
  const Mat4 expected = referenceModelMatrix(t);
  for (int i = 0; i < 16; ++i) {
    EXPECT_NEAR(data[4 + i], value_ptr(expected)[i], kEps) << i;
  }
}
