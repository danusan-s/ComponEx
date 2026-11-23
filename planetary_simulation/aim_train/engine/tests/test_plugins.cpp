#include "componeng/core/engine.hpp"
#include "componeng/core/plugin.hpp"
#include "componeng/ecs/system.hpp"
#include "componeng/ecs/world.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace componeng::core;
using namespace componeng::ecs;

namespace {

// What happened, in order, across systems and plugins.
std::vector<std::string> g_log;

template <int N> struct Marker final : ISystem {
  void onCreate(const SystemState &) override {
    g_log.push_back("create" + std::to_string(N));
  }
  void onUpdate(const SystemState &) override {
    g_log.push_back("update" + std::to_string(N));
  }
  void onDestroy(const SystemState &) override {
    g_log.push_back("destroy" + std::to_string(N));
  }
};

using A = Marker<1>;
using B = Marker<2>;
using C = Marker<3>;
using D = Marker<4>;

std::string joined(const std::vector<std::string> &log, const char *prefix) {
  std::string out;
  for (const std::string &entry : log) {
    if (entry.rfind(prefix, 0) == 0) {
      out += entry.substr(std::string(prefix).size());
    }
  }
  return out;
}

struct Fixture : ::testing::Test {
  void SetUp() override {
    g_log.clear();
  }
};

} // namespace

using Scheduler = Fixture;

TEST_F(Scheduler, KeepsRegistrationOrderWithoutConstraints) {
  Engine engine;
  engine.world().registerSystem<A>(SystemGroup::Simulation);
  engine.world().registerSystem<B>(SystemGroup::Simulation);
  engine.world().registerSystem<C>(SystemGroup::Simulation);
  engine.initialize();
  engine.update(0.1);
  EXPECT_EQ(joined(g_log, "update"), "123");
}

TEST_F(Scheduler, GroupsRunInOrderWhateverTheRegistrationOrder) {
  Engine engine;
  engine.world().registerSystem<A>(SystemGroup::FrameEnd);
  engine.world().registerSystem<B>(SystemGroup::Presentation);
  engine.world().registerSystem<C>(SystemGroup::PreUpdate);
  engine.world().registerSystem<D>(SystemGroup::Simulation);
  engine.initialize();
  engine.update(0.1);
  EXPECT_EQ(joined(g_log, "update"), "3421");
}

TEST_F(Scheduler, DependenciesMayBeAddedBeforeTheSystemExists) {
  Engine engine;
  engine.world().addSystemDependencies<A, B>(); // A after B, B not there yet
  engine.world().registerSystem<A>(SystemGroup::Simulation);
  engine.world().registerSystem<B>(SystemGroup::Simulation);
  engine.initialize();
  engine.update(0.1);
  EXPECT_EQ(joined(g_log, "update"), "21");
}

TEST_F(Scheduler, AddSystemBeforePutsASystemAheadOfOne) {
  Engine engine;
  engine.world().registerSystem<A>(SystemGroup::Simulation);
  engine.world().registerSystem<B>(SystemGroup::Simulation);
  engine.world().addSystemBefore<B, A>();
  engine.initialize();
  engine.update(0.1);
  EXPECT_EQ(joined(g_log, "update"), "21");
}

TEST_F(Scheduler, ADependencyInAnEarlierGroupIsNotRegisteredTwice) {
  Engine engine;
  engine.world().registerSystem<A>(SystemGroup::PreUpdate);
  engine.world().registerSystem<B>(SystemGroup::Simulation);
  engine.world().addSystemDependencies<B, A>();
  engine.initialize();
  engine.update(0.1);
  EXPECT_EQ(joined(g_log, "update"), "12"); // A once, not an extra copy
}

TEST_F(Scheduler, ADependencyOnALaterGroupThrows) {
  Engine engine;
  engine.world().registerSystem<A>(SystemGroup::Simulation);
  engine.world().registerSystem<B>(SystemGroup::Presentation);
  engine.world().addSystemDependencies<A, B>(); // A cannot follow a later group
  EXPECT_THROW(engine.initialize(), std::runtime_error);
}

TEST_F(Scheduler, AnUnknownDependencyThrows) {
  Engine engine;
  engine.world().registerSystem<A>(SystemGroup::Simulation);
  engine.world().addSystemDependencies<A, B>();
  EXPECT_THROW(engine.initialize(), std::runtime_error);
}

TEST_F(Scheduler, ACycleThrowsInsteadOfDroppingSystems) {
  Engine engine;
  engine.world().registerSystem<A>(SystemGroup::Simulation);
  engine.world().registerSystem<B>(SystemGroup::Simulation);
  engine.world().addSystemDependencies<A, B>();
  engine.world().addSystemDependencies<B, A>();
  try {
    engine.initialize();
    FAIL() << "expected a cycle error";
  } catch (const std::runtime_error &error) {
    EXPECT_NE(std::string(error.what()).find("cycle"), std::string::npos);
  }
}

TEST_F(Scheduler, RegisteringTwiceOrAfterCreationThrows) {
  Engine engine;
  engine.world().registerSystem<A>(SystemGroup::Simulation);
  EXPECT_THROW(engine.world().registerSystem<A>(SystemGroup::Presentation),
               std::runtime_error);
  engine.initialize();
  EXPECT_THROW(engine.world().registerSystem<B>(SystemGroup::Simulation),
               std::logic_error);
}

TEST_F(Scheduler, DestroysInReverseOfCreation) {
  Engine engine;
  engine.world().registerSystem<A>(SystemGroup::Simulation);
  engine.world().registerSystem<B>(SystemGroup::Simulation);
  engine.world().registerSystem<C>(SystemGroup::Simulation);
  engine.initialize();
  engine.shutdown();
  EXPECT_EQ(joined(g_log, "create"), "123");
  EXPECT_EQ(joined(g_log, "destroy"), "321");
}

namespace {

template <int N> struct Logging : IPlugin {
  void build(Engine &) override {
    g_log.push_back("build" + std::to_string(N));
  }
  void ready(World &) override {
    g_log.push_back("ready" + std::to_string(N));
  }
  void cleanup(World &) override {
    g_log.push_back("cleanup" + std::to_string(N));
  }
};

using P1 = Logging<1>;
using P2 = Logging<2>;
using P3 = Logging<3>;

// Needs P1 first.
struct NeedsP1 : IPlugin {
  void build(Engine &engine) override {
    engine.require<P1>();
    g_log.push_back("buildN");
  }
  void cleanup(World &) override {
    g_log.push_back("cleanupN");
  }
};

// Cannot be made without arguments.
struct Configured : IPlugin {
  explicit Configured(int value) : value(value) {
  }
  int value;
  void build(Engine &) override {
  }
};
struct NeedsConfigured : IPlugin {
  void build(Engine &engine) override {
    engine.require<Configured>();
  }
};

struct CycleA;
struct CycleB : IPlugin {
  void build(Engine &engine) override;
};
struct CycleA : IPlugin {
  void build(Engine &engine) override {
    engine.require<CycleB>();
  }
};
void CycleB::build(Engine &engine) {
  engine.require<CycleA>();
}

struct SetsAResource : IPlugin {
  void build(Engine &engine) override {
    engine.world().setResource<int>(42);
    engine.world().registerSystem<A>(SystemGroup::Simulation);
  }
};

} // namespace

using Plugins = Fixture;

TEST_F(Plugins, BuildWhenAddedAndReadyWhenInitialized) {
  Engine engine;
  engine.add(P1{}).add(P2{});
  EXPECT_EQ(g_log, (std::vector<std::string>{"build1", "build2"}));
  engine.initialize();
  EXPECT_EQ(g_log,
            (std::vector<std::string>{"build1", "build2", "ready1", "ready2"}));
}

TEST_F(Plugins, CleanUpInReverseAfterTheSystemsAreDestroyed) {
  Engine engine;
  engine.world().registerSystem<A>(SystemGroup::Simulation);
  engine.add(P1{}).add(P2{}).add(P3{});
  engine.initialize();
  g_log.clear();
  engine.shutdown();
  EXPECT_EQ(g_log, (std::vector<std::string>{"destroy1", "cleanup3", "cleanup2",
                                             "cleanup1"}));
  g_log.clear();
  engine.shutdown(); // a second call does nothing
  EXPECT_TRUE(g_log.empty());
}

TEST_F(Plugins, TheDestructorShutsDown) {
  {
    Engine engine;
    engine.add(P1{});
    engine.initialize();
    g_log.clear();
  }
  EXPECT_EQ(g_log, (std::vector<std::string>{"cleanup1"}));
}

TEST_F(Plugins, RequireBuildsTheDependencyFirstAndCleansItUpLast) {
  Engine engine;
  engine.add(NeedsP1{});
  EXPECT_TRUE(engine.has<P1>());
  engine.initialize();
  engine.shutdown();
  EXPECT_EQ(g_log, (std::vector<std::string>{"build1", "buildN", "ready1",
                                             "cleanupN", "cleanup1"}));
}

TEST_F(Plugins, RequireUsesThePluginYouAddedBefore) {
  Engine engine;
  engine.add(P1{}).add(NeedsP1{});
  EXPECT_EQ(g_log, (std::vector<std::string>{"build1", "buildN"}));
}

TEST_F(Plugins, AddingTheSameTypeTwiceThrows) {
  Engine engine;
  engine.add(P1{});
  EXPECT_THROW(engine.add(P1{}), std::runtime_error);
}

TEST_F(Plugins, AddingAfterInitializeThrows) {
  Engine engine;
  engine.initialize();
  EXPECT_THROW(engine.add(P1{}), std::logic_error);
}

TEST_F(Plugins, RequireThrowsForAPluginThatNeedsArguments) {
  Engine engine;
  EXPECT_THROW(engine.add(NeedsConfigured{}), std::runtime_error);
  Engine configured;
  configured.add(Configured{3}).add(NeedsConfigured{});
  EXPECT_TRUE(configured.has<Configured>());
}

TEST_F(Plugins, RequireThrowsOnACycle) {
  Engine engine;
  EXPECT_THROW(engine.add(CycleA{}), std::runtime_error);
}

TEST_F(Plugins, RegisterResourcesAndSystemsThroughTheWorld) {
  Engine engine;
  engine.add(SetsAResource{});
  EXPECT_EQ(engine.world().getResource<int>(), 42);
  engine.initialize();
  engine.update(0.5);
  engine.update(0.5);
  EXPECT_EQ(joined(g_log, "update"), "11");
  EXPECT_DOUBLE_EQ(engine.world().time, 1.0);
}

TEST_F(Plugins, UpdateBeforeInitializeThrows) {
  Engine engine;
  EXPECT_THROW(engine.update(0.1), std::logic_error);
}

TEST_F(Plugins, RunUsesTheRunnerThenShutsDown) {
  Engine engine;
  engine.add(P1{});
  int frames = 0;
  engine.setRunner([&](Engine &e) {
    for (; frames < 3; ++frames) {
      e.update(0.01);
    }
  });
  engine.run();
  EXPECT_EQ(frames, 3);
  EXPECT_EQ(g_log.back(), "cleanup1");
}

TEST_F(Plugins, TheDefaultRunnerLoopsHeadlessUntilExitIsRequested) {
  struct StopAfter : ISystem {
    int frames = 0;
    void onUpdate(const SystemState &state) override {
      if (++frames == 3) {
        state.world->getResource<EngineControl>().requestExit();
      }
    }
  };
  Engine engine;
  engine.world().registerSystem<StopAfter>(SystemGroup::Simulation);
  engine.run(); // returns once the system asks for exit
  EXPECT_NEAR(engine.world().time, 3.0 / 60.0, 1e-9);
}
