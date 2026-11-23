#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/backend/api/pass_id.hpp"
#include "componeng/renderer/batching/render_queue.hpp"
#include "componeng/renderer/draw/render_state_policy.hpp"

#include <gtest/gtest.h>

using namespace componeng::renderer;
using api::BlendMode;
using api::CullMode;
using api::DepthFunc;
using api::PassId;

namespace {

api::RenderState materialState(CullMode cull) {
  api::RenderState s;
  s.cull = cull;
  s.depthWrite = false;
  s.blend = BlendMode::Additive;
  return s;
}

} // namespace

TEST(RenderStatePolicy, PrepassWritesOnlyDepthAndKeepsMaterialCulling) {
  const auto s =
      renderStateFor(PassId::DepthPrepass, materialState(CullMode::Front),
                     /*depthPrepassed=*/true);
  EXPECT_EQ(s.cull, CullMode::Front);
  EXPECT_TRUE(s.depthTest);
  EXPECT_TRUE(s.depthWrite);
  EXPECT_EQ(s.depthFunc, DepthFunc::Less);
  EXPECT_EQ(s.blend, BlendMode::None);
  EXPECT_FALSE(s.colorWrite);
}

TEST(RenderStatePolicy, OpaqueWithoutPrepassWritesDepth) {
  const auto s = renderStateFor(PassId::Opaque, materialState(CullMode::Back),
                                /*depthPrepassed=*/false);
  EXPECT_EQ(s.depthFunc, DepthFunc::Less);
  EXPECT_TRUE(s.depthWrite);
  EXPECT_EQ(s.blend, BlendMode::None);
  EXPECT_TRUE(s.colorWrite);
}

TEST(RenderStatePolicy, OpaqueAfterPrepassTestsEqualAndLeavesDepthAlone) {
  const auto s = renderStateFor(PassId::Opaque, materialState(CullMode::None),
                                /*depthPrepassed=*/true);
  EXPECT_EQ(s.cull, CullMode::None);
  EXPECT_EQ(s.depthFunc, DepthFunc::Equal);
  EXPECT_FALSE(s.depthWrite);
}

TEST(RenderStatePolicy, SkyIgnoresMaterialAndDoesNotCullOrWriteDepth) {
  const auto s = renderStateFor(PassId::Sky, materialState(CullMode::Back),
                                /*depthPrepassed=*/true);
  EXPECT_EQ(s.cull, CullMode::None);
  EXPECT_EQ(s.depthFunc, DepthFunc::LessEqual);
  EXPECT_FALSE(s.depthWrite);
}

TEST(RenderStatePolicy,
     PostProcessIgnoresMaterialAndTestsNothingBlendsNothing) {
  const auto s =
      renderStateFor(PassId::PostProcess, materialState(CullMode::Back),
                     /*depthPrepassed=*/true);
  EXPECT_EQ(s.cull, CullMode::None);
  EXPECT_FALSE(s.depthTest);
  EXPECT_FALSE(s.depthWrite);
  EXPECT_EQ(s.blend, BlendMode::None);
}

TEST(RenderStatePolicy, TransparentUsesTheMaterialsStateUnchanged) {
  const api::RenderState m = materialState(CullMode::None);
  EXPECT_EQ(renderStateFor(PassId::Transparent, m, true), m);
}

TEST(RenderQueueOpaqueCount, SplitsSortedBatchesAtTheFirstTransparentOne) {
  RenderQueue queue;
  queue.openBatch(1, 1, 1, 1, /*transparent=*/true);
  queue.openBatch(2, 2, 1, 1, /*transparent=*/false);
  queue.openBatch(3, 3, 2, 1, /*transparent=*/true);
  queue.openBatch(4, 4, 2, 1, /*transparent=*/false);
  queue.sort();

  ASSERT_EQ(queue.size(), 4u);
  EXPECT_EQ(queue.opaqueCount(), 2u);
  EXPECT_FALSE(queue.sorted(1).transparent);
  EXPECT_TRUE(queue.sorted(2).transparent);
}

TEST(RenderQueueOpaqueCount, IsZeroForAnEmptyQueueAndAllWhenNothingIsBlended) {
  RenderQueue queue;
  queue.sort();
  EXPECT_EQ(queue.opaqueCount(), 0u);

  queue.openBatch(1, 1, 1, 1);
  queue.sort();
  EXPECT_EQ(queue.opaqueCount(), 1u);
}
