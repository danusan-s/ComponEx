#include "componeng/audio/audio_component.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/archetype/archetype.hpp"
#include "componeng/ecs/component_registry.hpp"
#include "componeng/ecs/entity.hpp"
#include "componeng/ecs/scene_serializer.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "gtest/gtest.h"

#include <cstdio>
#include <cstring>
#include <memory>

using namespace componeng::audio;
using namespace componeng::core;
using namespace componeng::ecs;
using namespace componeng::renderer;

template <typename T> ComponentID reg(ComponentRegistry &r) {
  return r.registerComponent<T>();
}

TEST(SerializationTest, RoundTripMeshComponent) {
  MeshComponent orig;
  orig.meshName = "test_cube";

  auto json = ComponentSerializer<MeshComponent>::serialize(orig);
  auto restored = ComponentSerializer<MeshComponent>::deserialize(json);

  EXPECT_EQ(orig.meshName, restored.meshName);
}

TEST(SerializationTest, RoundTripLongStringMesh) {
  MeshComponent orig;
  orig.meshName = "this_is_a_very_long_mesh_name_that_exceeds_SSO_limit";

  auto json = ComponentSerializer<MeshComponent>::serialize(orig);
  auto restored = ComponentSerializer<MeshComponent>::deserialize(json);

  EXPECT_EQ(orig.meshName, restored.meshName);
}

TEST(SerializationTest, RoundTripMaterialComponent) {
  MaterialComponent orig;
  orig.materialName = "brick_wall_material";

  auto json = ComponentSerializer<MaterialComponent>::serialize(orig);
  auto restored = ComponentSerializer<MaterialComponent>::deserialize(json);

  EXPECT_EQ(orig.materialName, restored.materialName);
}

TEST(SerializationTest, RoundTripAudioComponent) {
  AudioComponent orig;
  orig.audioName = "explosion_sound_effect_high_quality";
  orig.playOnAwake = false;
  orig.loop = true;
  orig.volume = 0.75f;

  auto json = ComponentSerializer<AudioComponent>::serialize(orig);
  auto restored = ComponentSerializer<AudioComponent>::deserialize(json);

  EXPECT_EQ(orig.audioName, restored.audioName);
  EXPECT_EQ(orig.playOnAwake, restored.playOnAwake);
  EXPECT_EQ(orig.loop, restored.loop);
  EXPECT_EQ(orig.volume, restored.volume);
}

TEST(SerializationTest, RoundTripColorComponent) {
  ColorComponent orig;
  orig.color = Vec4(0.25f, 0.5f, 0.75f, 1.0f);

  auto json = ComponentSerializer<ColorComponent>::serialize(orig);
  auto restored = ComponentSerializer<ColorComponent>::deserialize(json);

  EXPECT_FLOAT_EQ(orig.color.x, restored.color.x);
  EXPECT_FLOAT_EQ(orig.color.y, restored.color.y);
  EXPECT_FLOAT_EQ(orig.color.z, restored.color.z);
  EXPECT_FLOAT_EQ(orig.color.w, restored.color.w);
}

TEST(SerializationTest, RoundTripTransformComponent) {
  TransformComponent orig;
  orig.position = Vec3(1.0f, -2.0f, 3.5f);
  orig.rotation = Vec3(0.0f, 90.0f, 45.0f);
  orig.scale = Vec3(2.0f, 2.0f, 2.0f);

  auto json = ComponentSerializer<TransformComponent>::serialize(orig);
  auto restored = ComponentSerializer<TransformComponent>::deserialize(json);

  EXPECT_EQ(orig.position, restored.position);
  EXPECT_EQ(orig.rotation, restored.rotation);
  EXPECT_EQ(orig.scale, restored.scale);
}

TEST(SerializationTest, ArchetypeMigrationPreservesLongStrings) {
  ComponentRegistry reg;
  ComponentID meshCID = reg.registerComponent<MeshComponent>();

  Signature sig1;
  sig1.set(meshCID);
  Archetype arch1;
  arch1.init(sig1, reg);

  arch1.addEntity(1);
  void *slot1 = arch1.writeColumn(meshCID).at(0);
  new (slot1) MeshComponent();
  static_cast<MeshComponent *>(slot1)->meshName =
      "very_long_mesh_name_that_exceeds_sso_for_testing_purposes";

  // Migrate to same-signature archetype using memcpy (safe for
  // trivially-copyable Name)
  Signature sig2 = sig1;
  Archetype arch2;
  arch2.init(sig2, reg);
  arch2.addEntity(1);
  std::memcpy(arch2.writeColumn(meshCID).at(0),
              arch1.writeColumn(meshCID).at(0),
              arch1.writeColumn(meshCID).m_stride);
  arch1.removeEntityAtRow(0);

  auto &moved =
      static_cast<MeshComponent *>(arch2.writeColumn(meshCID).at(0))->meshName;
  EXPECT_EQ(moved, "very_long_mesh_name_that_exceeds_sso_for_testing_purposes");

  auto json = ComponentSerializer<MeshComponent>::serialize(
      *static_cast<MeshComponent *>(arch2.writeColumn(meshCID).at(0)));
  EXPECT_EQ(json["meshName"],
            "very_long_mesh_name_that_exceeds_sso_for_testing_purposes");
}

TEST(SerializationTest, MultipleEntityMigrations) {
  ComponentRegistry reg;
  ComponentID meshCID = reg.registerComponent<MeshComponent>();

  Signature sig1;
  sig1.set(meshCID);
  Archetype arch1;
  arch1.init(sig1, reg);

  for (int i = 0; i < 3; ++i) {
    arch1.addEntity(i + 10);
    void *slot = arch1.writeColumn(meshCID).at(i);
    new (slot) MeshComponent();
    static_cast<MeshComponent *>(slot)->meshName = Name(
        ("entity_" + std::to_string(i) + "_with_a_long_name_to_test").c_str());
  }

  // Migrate using memcpy + removeEntityAtRow (the archetype swap pattern)
  Signature sig2 = sig1;
  Archetype arch2;
  arch2.init(sig2, reg);
  // entity 10 is at row 0 in arch1; migrate it to arch2
  arch2.addEntity(10);
  std::memcpy(arch2.writeColumn(meshCID).at(0),
              arch1.writeColumn(meshCID).at(0),
              arch1.writeColumn(meshCID).m_stride);
  arch1.removeEntityAtRow(0);

  // entity 11 is now at row 0 (entity 10 was at 0, removed, entity 12
  // was at row 2 and got swapped to row 0). entity 11 remains at row 1.
  arch2.addEntity(11);
  std::memcpy(arch2.writeColumn(meshCID).at(1),
              arch1.writeColumn(meshCID).at(1),
              arch1.writeColumn(meshCID).m_stride);
  arch1.removeEntityAtRow(1);

  auto &moved =
      static_cast<MeshComponent *>(arch2.writeColumn(meshCID).at(0))->meshName;
  EXPECT_EQ(moved, "entity_0_with_a_long_name_to_test");

  auto json = ComponentSerializer<MeshComponent>::serialize(
      *static_cast<MeshComponent *>(arch2.writeColumn(meshCID).at(0)));
  EXPECT_EQ(json["meshName"], "entity_0_with_a_long_name_to_test");
}

namespace {

std::unique_ptr<World> makeSceneWorld() {
  auto world = std::make_unique<World>();
  world->registerComponents<TransformComponent, MeshComponent,
                            MaterialComponent, ColorComponent>();
  return world;
}

} // namespace

// End-to-end through SceneSerializer: this is what the editor's Save/Open does.
// Also exercises ComponentInfo::deleter — under ASan a regression here surfaces
// as a leak report rather than a failed assertion.
TEST(SerializationTest, SceneRoundTripPreservesTransformAndColor) {
  const std::string path = "test_scene_roundtrip.json";

  {
    auto world = makeSceneWorld();
    EntityID entity = world->createEntity();
    world->addComponents(
        entity,
        TransformComponent{.position = Vec3(10.0f, -4.0f, 2.5f),
                           .rotation = Vec3(0.0f, 90.0f, 0.0f),
                           .scale = Vec3(3.0f)},
        MeshComponent{.meshName = "cube"},
        MaterialComponent{.materialName = "default_diffuse"},
        ColorComponent{.color = Vec4(0.25f, 0.5f, 0.75f, 1.0f)});

    ASSERT_TRUE(SceneSerializer::save(*world, path));
  }

  auto world = makeSceneWorld();
  ASSERT_TRUE(SceneSerializer::load(*world, path));

  int count = 0;
  world->query().read<TransformComponent, MeshComponent, ColorComponent>().each(
      [&](const TransformComponent &t, const MeshComponent &m,
          const ColorComponent &c) {
        ++count;
        EXPECT_EQ(t.position, Vec3(10.0f, -4.0f, 2.5f));
        EXPECT_EQ(t.rotation, Vec3(0.0f, 90.0f, 0.0f));
        EXPECT_EQ(t.scale, Vec3(3.0f));
        EXPECT_EQ(m.meshName, "cube");
        EXPECT_FLOAT_EQ(c.color.x, 0.25f);
        EXPECT_FLOAT_EQ(c.color.y, 0.5f);
        EXPECT_FLOAT_EQ(c.color.z, 0.75f);
        EXPECT_FLOAT_EQ(c.color.w, 1.0f);
      });
  EXPECT_EQ(count, 1);

  std::remove(path.c_str());
}

TEST(SerializationTest, LoadReportsFailureForMissingFile) {
  auto world = makeSceneWorld();
  EXPECT_FALSE(SceneSerializer::load(*world, "no_such_scene_file.json"));
}
