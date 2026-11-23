#include "render_scene_fixture.hpp"

#include "componeng/renderer/component/procedural_instances_component.hpp"
#include "gtest/gtest.h"

#include <cstring>

using namespace rscene;

namespace {

// Shaders with no per-instance attributes, so the bindings are the only thing
// being exercised.
std::unique_ptr<Scene> makeBindingScene() {
  return makeScene([](fake::Recorder &r) {
    r.instanceLayout = {};
    r.declaredBlocks.insert("Settings");
  });
}

api::TextureDesc dataTexture() {
  api::TextureDesc desc;
  desc.mipmaps = false;
  desc.minFilter = api::TextureFilter::Nearest;
  desc.magFilter = api::TextureFilter::Nearest;
  return desc;
}

bool setSampler(fake::Recorder &r, int shaderId, const std::string &name) {
  const auto &names = r.looseUniforms[shaderId];
  return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

// --- Textures from memory ----------------------------------------------------

TEST(TextureFormat, ChannelsAndByteSizes) {
  using api::TextureFormat;
  static_assert(api::textureChannels(TextureFormat::R8) == 1);
  static_assert(api::textureChannels(TextureFormat::RG16F) == 2);
  static_assert(api::textureChannels(TextureFormat::RGB8) == 3);
  static_assert(api::textureChannels(TextureFormat::RGBA32F) == 4);
  static_assert(!api::textureIsFloat(TextureFormat::RGBA8));
  static_assert(api::textureIsFloat(TextureFormat::R16F));
  static_assert(api::textureCpuPixelBytes(TextureFormat::RGB8) == 3);
  // Half-float textures are still supplied as 32-bit floats on the CPU.
  static_assert(api::textureCpuPixelBytes(TextureFormat::R16F) == 4);
  static_assert(api::textureCpuPixelBytes(TextureFormat::RGBA32F) == 16);
  SUCCEED();
}

TEST(CreateTexture, RecordsFormatSizeDescAndPixels) {
  auto s = makeBindingScene();
  const float heights[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  s->assets().createTexture("heights", 2, 2, api::TextureFormat::R32F, heights,
                            dataTexture());

  ASSERT_TRUE(s->assets().textureExists("heights"));
  const auto &created = s->rec().createdTextures;
  ASSERT_EQ(created.size(), 1u);
  EXPECT_EQ(created[0].width, 2u);
  EXPECT_EQ(created[0].height, 2u);
  EXPECT_EQ(created[0].format, api::TextureFormat::R32F);
  EXPECT_FALSE(created[0].desc.mipmaps);
  EXPECT_EQ(created[0].desc.magFilter, api::TextureFilter::Nearest);
  ASSERT_EQ(created[0].pixels.size(), sizeof(heights));
  EXPECT_EQ(std::memcmp(created[0].pixels.data(), heights, sizeof(heights)), 0);

  const auto &tex = s->assets().getTexture(s->assets().getTextureID("heights"));
  EXPECT_EQ(tex.width(), 2u);
  EXPECT_EQ(tex.height(), 2u);
}

TEST(CreateTexture, NullPixelsOnlyAllocate) {
  auto s = makeBindingScene();
  s->assets().createTexture("empty", 8, 8, api::TextureFormat::RGBA16F,
                            nullptr);
  ASSERT_EQ(s->rec().createdTextures.size(), 1u);
  EXPECT_FALSE(s->rec().createdTextures[0].hadPixels);
}

TEST(UpdateTexture, ForwardsTheRegionAndItsPixels) {
  auto s = makeBindingScene();
  const unsigned char zeros[16] = {};
  s->assets().createTexture("mask", 4, 4, api::TextureFormat::R8, zeros);
  const unsigned char patch[4] = {10, 20, 30, 40};

  EXPECT_TRUE(s->assets().updateTexture(s->assets().getTextureID("mask"), 1, 2,
                                        2, 2, patch));
  ASSERT_EQ(s->rec().textureUpdates.size(), 1u);
  const auto &u = s->rec().textureUpdates[0];
  EXPECT_EQ(u.x, 1u);
  EXPECT_EQ(u.y, 2u);
  EXPECT_EQ(u.width, 2u);
  EXPECT_EQ(u.height, 2u);
  EXPECT_EQ(u.pixels, std::vector<unsigned char>(patch, patch + 4));
}

TEST(UpdateTexture, RegionsOutsideTheTextureAreRejected) {
  auto s = makeBindingScene();
  const unsigned char zeros[16] = {};
  s->assets().createTexture("mask", 4, 4, api::TextureFormat::R8, zeros);
  const HandleID id = s->assets().getTextureID("mask");
  const unsigned char patch[16] = {};

  EXPECT_FALSE(s->assets().updateTexture(id, 3, 3, 2, 2, patch)); // spills over
  EXPECT_FALSE(s->assets().updateTexture(id, 5, 0, 1, 1, patch)); // starts out
  EXPECT_FALSE(s->assets().updateTexture(id, 0, 0, 0, 2, patch)); // empty
  EXPECT_TRUE(s->rec().textureUpdates.empty());
  // The whole texture is still a valid region.
  EXPECT_TRUE(s->assets().updateTexture(id, 0, 0, 4, 4, patch));
}

// --- Buffers -----------------------------------------------------------------

TEST(Buffers, CreateUpdateAndRangeChecks) {
  auto s = makeBindingScene();
  const float values[4] = {1, 2, 3, 4};
  s->assets().createBuffer("values", values, sizeof(values));
  ASSERT_TRUE(s->assets().bufferExists("values"));
  const HandleID id = s->assets().getBufferID("values");
  EXPECT_EQ(s->assets().getBuffer(id).size(), sizeof(values));

  const float patch = 9.0f;
  EXPECT_TRUE(
      s->assets().updateBuffer(id, sizeof(float), &patch, sizeof(patch)));
  EXPECT_FALSE(
      s->assets().updateBuffer(id, sizeof(values), &patch, sizeof(patch)));
  EXPECT_FALSE(s->assets().bufferExists("nope"));
}

// --- Material bindings -------------------------------------------------------

TEST(MaterialTextures, BoundOnTheFirstExtraUnitWithTheSamplerSet) {
  auto s = makeBindingScene();
  const float heights[4] = {};
  s->assets().createTexture("heights", 2, 2, api::TextureFormat::R32F, heights);
  const HandleID tex = s->assets().getTextureID("heights");
  s->assets()
      .getMaterial(s->assets().getMaterialID("matA"))
      .setTexture("heightMap", tex);

  s->spawn("tri", "matA");
  s->frame();

  ASSERT_EQ(s->rec().draws.size(), 1u);
  const auto &d = s->rec().draws[0];
  ASSERT_TRUE(d.textureUnits.count(api::kFirstMaterialTextureUnit));
  EXPECT_EQ(d.textureUnits.at(api::kFirstMaterialTextureUnit), int(tex));
  EXPECT_TRUE(setSampler(s->rec(), d.shaderId, "heightMap"));
}

TEST(MaterialTextures, SeveralTakeConsecutiveUnitsAndSettingAgainReplaces) {
  auto s = makeBindingScene();
  const float one = 0.0f;
  s->assets().createTexture("a", 1, 1, api::TextureFormat::R32F, &one);
  s->assets().createTexture("b", 1, 1, api::TextureFormat::R32F, &one);
  s->assets().createTexture("c", 1, 1, api::TextureFormat::R32F, &one);
  auto &mat = s->assets().getMaterial(s->assets().getMaterialID("matA"));
  mat.setTexture("first", s->assets().getTextureID("a"));
  mat.setTexture("second", s->assets().getTextureID("b"));
  mat.setTexture("first", s->assets().getTextureID("c")); // replaces "a"

  EXPECT_EQ(mat.getTextureBindings().size(), 2u);

  s->spawn("tri", "matA");
  s->frame();

  const auto &d = s->rec().draws.at(0);
  const uint32_t u0 = api::kFirstMaterialTextureUnit;
  EXPECT_EQ(d.textureUnits.at(u0), int(s->assets().getTextureID("c")));
  EXPECT_EQ(d.textureUnits.at(u0 + 1), int(s->assets().getTextureID("b")));
}

TEST(MaterialTextures, OtherMaterialsDoNotGetThem) {
  auto s = makeBindingScene();
  const float one = 0.0f;
  s->assets().createTexture("a", 1, 1, api::TextureFormat::R32F, &one);
  s->assets()
      .getMaterial(s->assets().getMaterialID("matA"))
      .setTexture("heightMap", s->assets().getTextureID("a"));

  s->spawn("tri", "matB"); // shader "b": its sampler is never set
  s->frame();

  const auto &d = s->rec().draws.at(0);
  EXPECT_FALSE(setSampler(s->rec(), d.shaderId, "heightMap"));
}

TEST(MaterialUniformBlocks, BufferBoundAtTheFirstMaterialBinding) {
  auto s = makeBindingScene();
  const float settings[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  s->assets().createBuffer("settings", settings, sizeof(settings));
  s->assets()
      .getMaterial(s->assets().getMaterialID("matA"))
      .setUniformBuffer("Settings", s->assets().getBufferID("settings"));

  s->spawn("tri", "matA");
  s->frame();

  const fake::UniformBufferBind *bound = nullptr;
  for (const auto &b : s->rec().uniformBufferBinds) {
    if (b.binding == api::kFirstMaterialUniformBinding)
      bound = &b;
  }
  ASSERT_NE(bound, nullptr);
  ASSERT_EQ(bound->bytes.size(), sizeof(settings));
  EXPECT_EQ(std::memcmp(bound->bytes.data(), settings, sizeof(settings)), 0);

  bool attached = false;
  for (const auto &[name, binding] : s->rec().uniformBlockBinds) {
    if (name == "Settings" && binding == api::kFirstMaterialUniformBinding)
      attached = true;
  }
  EXPECT_TRUE(attached);
}

TEST(MaterialUniformBlocks, ABlockTheShaderLacksIsNotBound) {
  auto s = makeBindingScene();
  const float v = 1.0f;
  s->assets().createBuffer("other", &v, sizeof(v));
  s->assets()
      .getMaterial(s->assets().getMaterialID("matA"))
      .setUniformBuffer("NotInTheShader", s->assets().getBufferID("other"));

  s->spawn("tri", "matA");
  s->frame();

  for (const auto &b : s->rec().uniformBufferBinds) {
    EXPECT_NE(b.binding, api::kFirstMaterialUniformBinding);
  }
}

TEST(MaterialBufferTextures, BoundAfterTexturesWithTheirFormat) {
  auto s = makeBindingScene();
  const float one = 0.0f;
  s->assets().createTexture("heights", 1, 1, api::TextureFormat::R32F, &one);
  const float positions[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  s->assets().createBuffer("positions", positions, sizeof(positions));
  auto &mat = s->assets().getMaterial(s->assets().getMaterialID("matA"));
  mat.setBufferTexture("positions", s->assets().getBufferID("positions"),
                       api::BufferTextureFormat::RGBA32F);
  mat.setTexture("heightMap", s->assets().getTextureID("heights"));

  s->spawn("tri", "matA");
  s->frame();

  // Textures first, then buffer textures, on consecutive units.
  const uint32_t u0 = api::kFirstMaterialTextureUnit;
  const auto &d = s->rec().draws.at(0);
  EXPECT_EQ(d.textureUnits.at(u0), int(s->assets().getTextureID("heights")));
  ASSERT_EQ(s->rec().bufferTextureBinds.size(), 1u);
  const auto &b = s->rec().bufferTextureBinds[0];
  EXPECT_EQ(b.unit, u0 + 1);
  EXPECT_EQ(b.format, api::BufferTextureFormat::RGBA32F);
  EXPECT_EQ(b.bytes.size(), sizeof(positions));
  EXPECT_TRUE(setSampler(s->rec(), d.shaderId, "positions"));
}

TEST(MaterialTextures, WorkForProceduralDrawsToo) {
  auto s = makeBindingScene();
  const float one = 0.0f;
  s->assets().createTexture("heights", 1, 1, api::TextureFormat::R32F, &one);
  const HandleID tex = s->assets().getTextureID("heights");
  s->assets()
      .getMaterial(s->assets().getMaterialID("matA"))
      .setTexture("heightMap", tex);

  EntityID e = s->spawn("tri", "matA");
  s->world->addComponents(e, ProceduralInstancesComponent{.count = 100});
  s->frame();

  ASSERT_EQ(s->rec().draws.size(), 1u);
  EXPECT_EQ(s->rec().draws[0].instanceCount, 100u);
  EXPECT_EQ(s->rec().draws[0].textureUnits.at(api::kFirstMaterialTextureUnit),
            int(tex));
}
