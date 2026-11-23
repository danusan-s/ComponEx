#include "componeng/ecs/archetype/archetype.hpp"
#include <gtest/gtest.h>

struct Pos {
  static constexpr const char *component_name = "Pos";
  float x, y, z;
};
struct Vel {
  static constexpr const char *component_name = "Vel";
  float vx, vy, vz;
};

static componeng::ecs::ComponentRegistry makeRegistry() {
  componeng::ecs::ComponentRegistry reg;
  reg.registerComponent<Pos>();
  reg.registerComponent<Vel>();
  return reg;
}

TEST(ArchetypeTest, InitSetsSignature) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  EXPECT_EQ(arch.m_signature, sig);
}

TEST(ArchetypeTest, AddEntityIncreasesCount) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  arch.addEntity(0);
  arch.addEntity(1);

  EXPECT_EQ(arch.getEntityCount(), 2u);
}

TEST(ArchetypeTest, ReadColumnReturnsValidColumn) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  arch.addEntity(0);

  auto &col = arch.readColumn(reg.getComponentID<Pos>());
  EXPECT_EQ(col.m_count, 1u);
  EXPECT_EQ(col.m_stride, sizeof(Pos));
}

TEST(ArchetypeTest, GetReturnsCorrectComponent) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  arch.addEntity(0);

  Pos &p = arch.write<Pos>(reg.getComponentID<Pos>(), 0);
  p.x = 1.0f;
  p.y = 2.0f;
  p.z = 3.0f;

  Pos &p2 = arch.write<Pos>(reg.getComponentID<Pos>(), 0);
  EXPECT_FLOAT_EQ(p2.x, 1.0f);
  EXPECT_FLOAT_EQ(p2.y, 2.0f);
  EXPECT_FLOAT_EQ(p2.z, 3.0f);
}

TEST(ArchetypeTest, RemoveEntityDecreasesCount) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  arch.addEntity(0);
  arch.addEntity(1);
  arch.addEntity(2);

  arch.removeEntityAtRow(1);
  EXPECT_EQ(arch.getEntityCount(), 2u);
}

TEST(ArchetypeTest, RemoveEntityReturnsSwappedEntity) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  arch.addEntity(10);
  arch.addEntity(20);

  // Removing row 0 (entity 10) should swap entity 20 into its slot
  componeng::ecs::EntityID swapped = arch.removeEntityAtRow(0);
  EXPECT_EQ(swapped, 20u);
  EXPECT_EQ(arch.getEntityForRow(0), 20u);
}

TEST(ArchetypeTest, AddEntityReturnsDenseRows) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);

  EXPECT_EQ(arch.addEntity(5), 0u);
  EXPECT_EQ(arch.addEntity(10), 1u);
  EXPECT_EQ(arch.getEntityForRow(0), 5u);
  EXPECT_EQ(arch.getEntityForRow(1), 10u);
}

TEST(ArchetypeTest, ThrowsOnInvalidColumn) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  arch.addEntity(0);

  EXPECT_THROW(arch.readColumn(reg.getComponentID<Vel>()), std::runtime_error);
}

TEST(ArchetypeTest, MultiComponentArchetype) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());
  sig.set(reg.getComponentID<Vel>());

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  arch.addEntity(0);

  Pos &p = arch.write<Pos>(reg.getComponentID<Pos>(), 0);
  p.x = 5.0f;

  Vel &v = arch.write<Vel>(reg.getComponentID<Vel>(), 0);
  v.vx = 1.0f;

  EXPECT_FLOAT_EQ(arch.write<Pos>(reg.getComponentID<Pos>(), 0).x, 5.0f);
  EXPECT_FLOAT_EQ(arch.write<Vel>(reg.getComponentID<Vel>(), 0).vx, 1.0f);
}

TEST(ArchetypeTest, ColumnVersionTracksWrites) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());
  const auto pos = reg.getComponentID<Pos>();

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  EXPECT_EQ(arch.columnVersion(pos), 0u);

  // Adding a row changes the column.
  arch.addEntity(0);
  const auto afterAdd = arch.columnVersion(pos);
  EXPECT_GT(afterAdd, 0u);

  // Reading does not.
  (void)arch.readColumn(pos);
  (void)arch.read<Pos>(pos, 0);
  EXPECT_EQ(arch.columnVersion(pos), afterAdd);

  // Taking a writer does.
  arch.write<Pos>(pos, 0).x = 1.0f;
  const auto afterWrite = arch.columnVersion(pos);
  EXPECT_GT(afterWrite, afterAdd);
  arch.writeColumn(pos);
  EXPECT_GT(arch.columnVersion(pos), afterWrite);

  // Removing a row changes it too.
  const auto beforeRemove = arch.columnVersion(pos);
  arch.removeEntityAtRow(0);
  EXPECT_GT(arch.columnVersion(pos), beforeRemove);
}

TEST(ArchetypeTest, ColumnVersionsAreIndependentPerComponent) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());
  sig.set(reg.getComponentID<Vel>());
  const auto pos = reg.getComponentID<Pos>();
  const auto vel = reg.getComponentID<Vel>();

  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  arch.addEntity(0);

  const auto velBefore = arch.columnVersion(vel);
  arch.writeColumn(pos);
  EXPECT_EQ(arch.columnVersion(vel), velBefore);
}

TEST(ArchetypeTest, ReadColumnThrowsOnMissingComponent) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());
  componeng::ecs::Archetype arch;
  arch.init(sig, reg);
  EXPECT_THROW(arch.writeColumn(reg.getComponentID<Vel>()), std::runtime_error);
  EXPECT_THROW(arch.columnVersion(reg.getComponentID<Vel>()),
               std::runtime_error);
}
