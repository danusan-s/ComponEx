#include "componeng/ecs/archetype/archetype.hpp"
#include "componeng/ecs/query.hpp"
#include <gtest/gtest.h>

struct Pos {
  static constexpr const char *component_name = "Pos";
  float x, y, z;
};
struct Vel {
  static constexpr const char *component_name = "Vel";
  float vx, vy, vz;
};
struct Mass {
  static constexpr const char *component_name = "Mass";
  float value;
};

static componeng::ecs::ComponentRegistry makeRegistry() {
  componeng::ecs::ComponentRegistry reg;
  reg.registerComponent<Pos>();
  reg.registerComponent<Vel>();
  reg.registerComponent<Mass>();
  return reg;
}

// Number of archetypes makeArchetypes() actually initialises.
static constexpr std::size_t ARCHETYPE_COUNT = 3;

static std::array<componeng::ecs::Archetype, componeng::ecs::MAX_ARCHETYPES>
makeArchetypes(componeng::ecs::ComponentRegistry &reg) {
  std::array<componeng::ecs::Archetype, componeng::ecs::MAX_ARCHETYPES> archs;

  // NOTE: keep ARCHETYPE_COUNT in sync with the archetypes built below.
  // Archetype 0: Pos only
  componeng::ecs::Signature sig0;
  sig0.set(reg.getComponentID<Pos>());
  archs[0].init(sig0, reg);
  archs[0].addEntity(0);
  archs[0].addEntity(1);

  // Archetype 1: Pos + Vel
  componeng::ecs::Signature sig1;
  sig1.set(reg.getComponentID<Pos>());
  sig1.set(reg.getComponentID<Vel>());
  archs[1].init(sig1, reg);
  archs[1].addEntity(2);
  archs[1].addEntity(3);

  // Archetype 2: Pos + Vel + Mass
  componeng::ecs::Signature sig2;
  sig2.set(reg.getComponentID<Pos>());
  sig2.set(reg.getComponentID<Vel>());
  sig2.set(reg.getComponentID<Mass>());
  archs[2].init(sig2, reg);
  archs[2].addEntity(4);

  return archs;
}

TEST(QueryTest, MatchesRequiredComponents) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  auto q = componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg).write<Pos>();

  // Archetype 0 has Pos
  EXPECT_TRUE(q.matches(archs[0]));
  // Archetype 1 has Pos + Vel (superset of Pos)
  EXPECT_TRUE(q.matches(archs[1]));
  // Archetype 2 has Pos + Vel + Mass (superset of Pos)
  EXPECT_TRUE(q.matches(archs[2]));
}

TEST(QueryTest, MatchesMultipleRequiredComponents) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  auto q =
      componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg).write<Pos, Vel>();

  // Archetype 0 only has Pos, missing Vel
  EXPECT_FALSE(q.matches(archs[0]));
  // Archetype 1 has both
  EXPECT_TRUE(q.matches(archs[1]));
  // Archetype 2 has both (plus Mass)
  EXPECT_TRUE(q.matches(archs[2]));
}

TEST(QueryTest, ExcludeFiltersCorrectly) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  auto q =
      componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg).write<Pos, Vel>();
  q.exclude<Mass>();

  // Archetype 2 has Mass, should be excluded
  EXPECT_FALSE(q.matches(archs[2]));
  // Archetype 1 has Pos + Vel but no Mass
  EXPECT_TRUE(q.matches(archs[1]));
}

TEST(QueryTest, EachIteratesMatchingArchetypes) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  int count = 0;
  auto q = componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg).write<Pos>();
  q.each([&](Pos &p) { ++count; });

  // Archetype 0: 2 entities, Archetype 1: 2 entities, Archetype 2: 1 entity
  // All have Pos, so 5 total
  EXPECT_EQ(count, 5);
}

TEST(QueryTest, EachWithEntityProvidesEntityID) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  std::vector<componeng::ecs::EntityID> entities;
  auto q = componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg).write<Pos>();
  q.eachWithEntity([&](componeng::ecs::EntityID entity, Pos &p) {
    entities.push_back(entity);
  });

  EXPECT_EQ(entities.size(), 5u);
  EXPECT_EQ(entities[0], 0u);
  EXPECT_EQ(entities[1], 1u);
  EXPECT_EQ(entities[2], 2u);
}

TEST(QueryTest, ExcludeReturnsReference) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  auto q =
      componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg).write<Pos, Vel>();
  auto &ref = q.exclude<Mass>();

  // Should return the same object
  EXPECT_EQ(&ref, &q);
  // Exclusion should be applied
  EXPECT_FALSE(q.matches(archs[2]));
}

TEST(QueryTest, EachWithExclude) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  int count = 0;
  auto q =
      componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg).write<Pos, Vel>();
  q.exclude<Mass>();
  q.each([&](Pos &p, Vel &v) { ++count; });

  // Only archetype 1 matches (Pos + Vel, no Mass)
  EXPECT_EQ(count, 2);
}

// ---------------------------------------------------------------------------
// Read / write access and column versions
// ---------------------------------------------------------------------------

using componeng::ecs::ColumnVersion;

static ColumnVersion versionOf(componeng::ecs::Archetype &a,
                               componeng::ecs::ComponentRegistry &reg,
                               componeng::ecs::ComponentID id) {
  (void)reg;
  return a.columnVersion(id);
}

TEST(QueryAccessTest, CallbackArgsFollowDeclarationOrderAndConstness) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  int count = 0;
  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .write<Vel>()
      .read<Pos>()
      .each([&](Vel &v, const Pos &p) {
        static_assert(!std::is_const_v<std::remove_reference_t<decltype(v)>>);
        static_assert(std::is_const_v<std::remove_reference_t<decltype(p)>>);
        ++count;
      });
  EXPECT_EQ(count, 3); // archetypes 1 (2 entities) and 2 (1 entity)
}

TEST(QueryAccessTest, ReadOnlyQueryLeavesVersionsAlone) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);
  const auto pos = reg.getComponentID<Pos>();
  const auto vel = reg.getComponentID<Vel>();

  const ColumnVersion posBefore = versionOf(archs[1], reg, pos);
  const ColumnVersion velBefore = versionOf(archs[1], reg, vel);

  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .read<Pos, Vel>()
      .each([](const Pos &, const Vel &) {});

  EXPECT_EQ(versionOf(archs[1], reg, pos), posBefore);
  EXPECT_EQ(versionOf(archs[1], reg, vel), velBefore);
}

TEST(QueryAccessTest, WriteBumpsOnlyWrittenColumns) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);
  const auto pos = reg.getComponentID<Pos>();
  const auto vel = reg.getComponentID<Vel>();
  const auto mass = reg.getComponentID<Mass>();

  const ColumnVersion posBefore = versionOf(archs[2], reg, pos);
  const ColumnVersion velBefore = versionOf(archs[2], reg, vel);
  const ColumnVersion massBefore = versionOf(archs[2], reg, mass);

  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .write<Pos>()
      .read<Vel>()
      .each([](Pos &, const Vel &) {});

  EXPECT_GT(versionOf(archs[2], reg, pos), posBefore);
  EXPECT_EQ(versionOf(archs[2], reg, vel), velBefore);
  EXPECT_EQ(versionOf(archs[2], reg, mass), massBefore);
}

TEST(QueryAccessTest, WriteBumpsOncePerArchetypeNotPerRow) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);
  const auto pos = reg.getComponentID<Pos>();

  const ColumnVersion before = versionOf(archs[1], reg, pos); // 2 rows
  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .write<Pos>()
      .each([](Pos &) {});
  EXPECT_EQ(versionOf(archs[1], reg, pos), before + 1);
}

TEST(QueryAccessTest, UnmatchedArchetypesAreNotBumped) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);
  const auto pos = reg.getComponentID<Pos>();

  const ColumnVersion before = versionOf(archs[2], reg, pos);
  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .write<Pos, Vel>()
      .exclude<Mass>()
      .each([](Pos &, Vel &) {});
  EXPECT_EQ(versionOf(archs[2], reg, pos), before);
}

TEST(QueryAccessTest, EmptyArchetypeIsNotBumped) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);
  const auto mass = reg.getComponentID<Mass>();

  archs[2].removeEntityAtRow(0); // now empty; removal itself bumps
  const ColumnVersion before = versionOf(archs[2], reg, mass);
  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .write<Mass>()
      .each([](Mass &) {});
  EXPECT_EQ(versionOf(archs[2], reg, mass), before);
}

TEST(QueryAccessTest, WriteIsVisibleThroughRead) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .write<Pos>()
      .each([](Pos &p) { p.x = 7.0f; });

  float sum = 0.0f;
  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .read<Pos>()
      .each([&](const Pos &p) { sum += p.x; });
  EXPECT_FLOAT_EQ(sum, 35.0f);
}

TEST(QueryAccessTest, EachWithEntityBumpsWriteColumns) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);
  const auto vel = reg.getComponentID<Vel>();

  const ColumnVersion before = versionOf(archs[1], reg, vel);
  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .write<Vel>()
      .eachWithEntity([](componeng::ecs::EntityID, Vel &) {});
  EXPECT_EQ(versionOf(archs[1], reg, vel), before + 1);
}

TEST(QueryAccessTest, EachParallelWritesAndBumpsOnce) {
  componeng::ecs::ComponentRegistry reg;
  reg.registerComponent<Pos>();
  std::array<componeng::ecs::Archetype, componeng::ecs::MAX_ARCHETYPES> archs;
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());
  archs[0].init(sig, reg);
  constexpr int kRows = 1000;
  for (int i = 0; i < kRows; ++i)
    archs[0].addEntity(static_cast<componeng::ecs::EntityID>(i));

  const auto pos = reg.getComponentID<Pos>();
  const ColumnVersion before = archs[0].columnVersion(pos);

  componeng::ecs::ThreadPool pool(4);
  componeng::ecs::Query<>(archs, 1, reg)
      .write<Pos>()
      .eachParallel(pool, [](Pos &p) { p.x = 3.0f; });

  EXPECT_EQ(archs[0].columnVersion(pos), before + 1);
  float sum = 0.0f;
  componeng::ecs::Query<>(archs, 1, reg).read<Pos>().each([&](const Pos &p) {
    sum += p.x;
  });
  EXPECT_FLOAT_EQ(sum, 3.0f * kRows);
}

TEST(QueryAccessTest, ExcludeAccumulates) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  auto q = componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg).read<Pos>();
  q.exclude<Vel>();
  q.exclude<Mass>();
  EXPECT_TRUE(q.matches(archs[0]));
  EXPECT_FALSE(q.matches(archs[1]));
  EXPECT_FALSE(q.matches(archs[2]));
}

TEST(QueryAccessTest, RangeForIteratesAndVersionsWrites) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);
  const auto pos = reg.getComponentID<Pos>();
  const auto vel = reg.getComponentID<Vel>();

  const ColumnVersion posBefore = versionOf(archs[1], reg, pos);
  const ColumnVersion velBefore = versionOf(archs[1], reg, vel);

  auto q = componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
               .write<Pos>()
               .read<Vel>();
  int count = 0;
  for (auto [p, v] : q) {
    p.x = 1.0f;
    (void)v;
    ++count;
  }
  EXPECT_EQ(count, 3);
  EXPECT_EQ(versionOf(archs[1], reg, pos), posBefore + 1);
  EXPECT_EQ(versionOf(archs[1], reg, vel), velBefore);
}

TEST(QueryAccessTest, RangeForSkipsEmptyArchetypes) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);
  archs[0].removeEntityAtRow(1);
  archs[0].removeEntityAtRow(0); // first matching archetype is now empty

  auto q = componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg).read<Pos>();
  int count = 0;
  for (auto [p] : q) {
    (void)p;
    ++count;
  }
  EXPECT_EQ(count, 3);
}

// ---------------------------------------------------------------------------
// Chunks
// ---------------------------------------------------------------------------

TEST(QueryChunkTest, EachChunkVisitsEveryNonEmptyArchetypeOnce) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);
  archs[0].removeEntityAtRow(1);
  archs[0].removeEntityAtRow(0); // empty: must not be visited

  std::vector<size_t> sizes;
  std::vector<componeng::ecs::ArchetypeID> ids;
  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .read<Pos>()
      .eachChunk([&](const auto &chunk) {
        sizes.push_back(chunk.size());
        ids.push_back(chunk.archetypeId());
        EXPECT_EQ(chunk.begin(), 0u);
        EXPECT_EQ(chunk.end(), chunk.size());
      });

  EXPECT_EQ(sizes, (std::vector<size_t>{2, 1}));
  EXPECT_EQ(ids, (std::vector<componeng::ecs::ArchetypeID>{1, 2}));
}

TEST(QueryChunkTest, DataIsARowIndexedArrayWithDeclaredConstness) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .write<Pos>()
      .read<Vel>()
      .eachChunk([&](const auto &chunk) {
        static_assert(
            std::is_same_v<decltype(chunk.template data<Pos>()), Pos *>);
        static_assert(
            std::is_same_v<decltype(chunk.template data<Vel>()), const Vel *>);
        Pos *pos = chunk.template data<Pos>();
        for (size_t row = chunk.begin(); row < chunk.end(); ++row)
          pos[row].x = static_cast<float>(chunk.entity(row));
      });

  // Only archetypes holding Vel (entities 2, 3, 4) were written.
  float sum = 0.0f;
  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .read<Pos, Vel>()
      .eachWithEntity(
          [&](componeng::ecs::EntityID e, const Pos &p, const Vel &) {
            EXPECT_FLOAT_EQ(p.x, static_cast<float>(e));
            sum += p.x;
          });
  EXPECT_FLOAT_EQ(sum, 2 + 3 + 4);
}

TEST(QueryChunkTest, ChunkReportsTheColumnVersion) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);

  componeng::ecs::ColumnVersion seen = 0;
  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .read<Pos>()
      .exclude<Vel>()
      .eachChunk(
          [&](const auto &chunk) { seen = chunk.template version<Pos>(); });
  EXPECT_EQ(seen, archs[0].columnVersion(reg.getComponentID<Pos>()));
}

TEST(QueryChunkTest, ReadChunkLeavesVersionsAloneWriteChunkBumpsOnce) {
  componeng::ecs::ComponentRegistry reg = makeRegistry();
  auto archs = makeArchetypes(reg);
  const auto pos = reg.getComponentID<Pos>();
  const auto vel = reg.getComponentID<Vel>();

  const auto posBefore = archs[1].columnVersion(pos);
  const auto velBefore = archs[1].columnVersion(vel);
  componeng::ecs::Query<>(archs, ARCHETYPE_COUNT, reg)
      .write<Pos>()
      .read<Vel>()
      .eachChunk([](const auto &) {});
  EXPECT_EQ(archs[1].columnVersion(pos), posBefore + 1);
  EXPECT_EQ(archs[1].columnVersion(vel), velBefore);
}

TEST(QueryChunkTest, ParallelChunksCoverEveryRowExactlyOnce) {
  componeng::ecs::ComponentRegistry reg;
  reg.registerComponent<Pos>();
  std::array<componeng::ecs::Archetype, componeng::ecs::MAX_ARCHETYPES> archs;
  componeng::ecs::Signature sig;
  sig.set(reg.getComponentID<Pos>());
  archs[0].init(sig, reg);
  constexpr size_t kRows = 1003; // not a multiple of any chunk size
  for (size_t i = 0; i < kRows; ++i)
    archs[0].addEntity(static_cast<componeng::ecs::EntityID>(i));

  // Column memory starts uninitialised, so zero it first.
  componeng::ecs::Query<>(archs, 1, reg).write<Pos>().each([](Pos &p) {
    p.x = 0.0f;
  });

  const auto pos = reg.getComponentID<Pos>();
  const auto before = archs[0].columnVersion(pos);

  componeng::ecs::ThreadPool pool(4);
  componeng::ecs::Query<>(archs, 1, reg)
      .write<Pos>()
      .eachChunkParallel(pool, [](const auto &chunk) {
        Pos *p = chunk.template data<Pos>();
        for (size_t row = chunk.begin(); row < chunk.end(); ++row)
          p[row].x += 1.0f; // twice would show as 2
      });

  EXPECT_EQ(archs[0].columnVersion(pos), before + 1);
  size_t visited = 0;
  componeng::ecs::Query<>(archs, 1, reg).read<Pos>().each([&](const Pos &p) {
    ++visited;
    EXPECT_FLOAT_EQ(p.x, 1.0f);
  });
  EXPECT_EQ(visited, kRows);
}
