#include "componeng/ecs/component_registry.hpp"
#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

struct TestCompA {
  static constexpr const char *component_name = "TestCompA";
  int value;
};
struct TestCompB {
  static constexpr const char *component_name = "TestCompB";
  float x, y;
};
struct TestCompC {
  static constexpr const char *component_name = "TestCompC";
  double data[4];
};

TEST(ComponentRegistryTest, RegistersUniqueIDs) {
  componeng::ecs::ComponentRegistry reg;
  componeng::ecs::ComponentID idA = reg.registerComponent<TestCompA>();
  componeng::ecs::ComponentID idB = reg.registerComponent<TestCompB>();
  componeng::ecs::ComponentID idC = reg.registerComponent<TestCompC>();

  EXPECT_NE(idA, idB);
  EXPECT_NE(idB, idC);
  EXPECT_NE(idA, idC);
}

TEST(ComponentRegistryTest, ReturnsSameIDOnDuplicateRegister) {
  componeng::ecs::ComponentRegistry reg;
  componeng::ecs::ComponentID id1 = reg.registerComponent<TestCompA>();
  componeng::ecs::ComponentID id2 = reg.registerComponent<TestCompA>();
  EXPECT_EQ(id1, id2);
}

TEST(ComponentRegistryTest, GetComponentIDMatchesRegister) {
  componeng::ecs::ComponentRegistry reg;
  componeng::ecs::ComponentID regID = reg.registerComponent<TestCompA>();
  componeng::ecs::ComponentID getID = reg.getComponentID<TestCompA>();
  EXPECT_EQ(regID, getID);
}

TEST(ComponentRegistryTest, GetComponentCount) {
  componeng::ecs::ComponentRegistry reg;
  EXPECT_EQ(reg.getComponentCount(), 0u);
  reg.registerComponent<TestCompA>();
  EXPECT_EQ(reg.getComponentCount(), 1u);
  reg.registerComponent<TestCompB>();
  EXPECT_EQ(reg.getComponentCount(), 2u);
}

TEST(ComponentRegistryTest, GetComponentInfo) {
  componeng::ecs::ComponentRegistry reg;
  componeng::ecs::ComponentID id = reg.registerComponent<TestCompA>();
  componeng::ecs::ComponentInfo &info = reg.getComponentInfo(id);
  EXPECT_EQ(info.size, sizeof(TestCompA));
}

TEST(ComponentRegistryTest, MakeSignature) {
  componeng::ecs::ComponentRegistry reg;
  reg.registerComponent<TestCompA>();
  reg.registerComponent<TestCompB>();

  componeng::ecs::Signature sig = reg.makeSignature<TestCompA, TestCompB>();
  EXPECT_TRUE(sig.test(reg.getComponentID<TestCompA>()));
  EXPECT_TRUE(sig.test(reg.getComponentID<TestCompB>()));
  EXPECT_EQ(sig.count(), 2u);
}

TEST(ComponentRegistryTest, ThrowsOnUnregisteredType) {
  componeng::ecs::ComponentRegistry reg;
  EXPECT_THROW(reg.getComponentID<TestCompA>(), std::runtime_error);
}

struct NonTrivial {
  static constexpr const char *component_name = "NonTrivial";
  NonTrivial() : ptr(new int(42)) {
  }
  ~NonTrivial() {
    delete ptr;
  }
  int *ptr;
};

TEST(ComponentRegistryTest, StoresDestructorForNonTrivialTypes) {
  componeng::ecs::ComponentRegistry reg;
  componeng::ecs::ComponentID id = reg.registerComponent<NonTrivial>();
  componeng::ecs::ComponentInfo &info = reg.getComponentInfo(id);
  EXPECT_NE(info.destructor, nullptr);
}

TEST(ComponentRegistryTest, NoDestructorForTrivialTypes) {
  componeng::ecs::ComponentRegistry reg;
  componeng::ecs::ComponentID id = reg.registerComponent<TestCompA>();
  componeng::ecs::ComponentInfo &info = reg.getComponentInfo(id);
  EXPECT_EQ(info.destructor, nullptr);
}

// --- ids are per registry, lookups are per type ------------------------------
//
// getComponentID() finds a type's id by indexing an array with a small integer
// that is unique to the C++ type. These tests pin down the property that makes
// that safe: the type index is global, the ComponentID is not.

TEST(ComponentRegistryTest, SameTypeGetsDifferentIdsInDifferentRegistries) {
  componeng::ecs::ComponentRegistry first;
  first.registerComponent<TestCompA>();
  first.registerComponent<TestCompB>();

  componeng::ecs::ComponentRegistry second;
  second.registerComponent<TestCompB>(); // opposite order
  second.registerComponent<TestCompA>();

  EXPECT_EQ(first.getComponentID<TestCompA>(), 0);
  EXPECT_EQ(first.getComponentID<TestCompB>(), 1);
  EXPECT_EQ(second.getComponentID<TestCompB>(), 0);
  EXPECT_EQ(second.getComponentID<TestCompA>(), 1);

  // And querying one registry again after using the other still answers for
  // that registry, not the last one asked.
  EXPECT_EQ(first.getComponentID<TestCompA>(), 0);
}

TEST(ComponentRegistryTest, TypeRegisteredElsewhereIsStillUnregisteredHere) {
  componeng::ecs::ComponentRegistry has;
  has.registerComponent<TestCompA>();
  has.registerComponent<TestCompB>();

  componeng::ecs::ComponentRegistry lacks;
  lacks.registerComponent<TestCompC>();

  EXPECT_THROW(lacks.getComponentID<TestCompA>(), std::runtime_error);
  EXPECT_THROW(lacks.getComponentID<TestCompB>(), std::runtime_error);
  EXPECT_NO_THROW(lacks.getComponentID<TestCompC>());
  EXPECT_THROW(has.getComponentID<TestCompC>(), std::runtime_error);
}

TEST(ComponentRegistryTest, RegisteringLaterDoesNotDisturbEarlierLookups) {
  componeng::ecs::ComponentRegistry reg;
  const auto a = reg.registerComponent<TestCompA>();
  // C's type index is not adjacent to A's; registering it grows the table.
  const auto c = reg.registerComponent<TestCompC>();
  const auto b = reg.registerComponent<TestCompB>();

  EXPECT_EQ(reg.getComponentID<TestCompA>(), a);
  EXPECT_EQ(reg.getComponentID<TestCompB>(), b);
  EXPECT_EQ(reg.getComponentID<TestCompC>(), c);
}

TEST(ComponentRegistryTest, CvAndReferenceQualifiedTypesResolveToTheSameId) {
  componeng::ecs::ComponentRegistry reg;
  const auto id = reg.registerComponent<TestCompA>();
  EXPECT_EQ(reg.getComponentID<const TestCompA>(), id);
  EXPECT_EQ(reg.getComponentID<TestCompA &>(), id);
  EXPECT_EQ(reg.getComponentID<const TestCompA &>(), id);
}

TEST(ComponentRegistryTest, LookupsByNameStillAgreeWithLookupsByType) {
  componeng::ecs::ComponentRegistry reg;
  reg.registerComponent<TestCompB>();
  reg.registerComponent<TestCompA>();
  EXPECT_EQ(reg.getComponentIDByName("TestCompA"),
            reg.getComponentID<TestCompA>());
  EXPECT_EQ(reg.getComponentIDByName("TestCompB"),
            reg.getComponentID<TestCompB>());
}

TEST(ComponentRegistryTest, ConcurrentLookupsAreConsistent) {
  // Queries run on thread-pool workers while the registry is only read.
  componeng::ecs::ComponentRegistry reg;
  const auto a = reg.registerComponent<TestCompA>();
  const auto b = reg.registerComponent<TestCompB>();
  const auto c = reg.registerComponent<TestCompC>();

  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 200000; ++i) {
        if (reg.getComponentID<TestCompA>() != a ||
            reg.getComponentID<TestCompB>() != b ||
            reg.getComponentID<TestCompC>() != c) {
          ++wrong;
        }
      }
    });
  }
  for (auto &th : threads) {
    th.join();
  }
  EXPECT_EQ(wrong.load(), 0);
}
