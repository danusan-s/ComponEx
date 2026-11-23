#pragma once

#include "componeng/ecs/system.hpp"
#include "componeng/utils/logger.hpp"

#include <assert.h>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#include <cstdlib>
#include <cxxabi.h>
#endif

namespace componeng::ecs {

namespace detail {

/** "N10componeng8renderer14CullingSystemE" -> "CullingSystem". */
inline std::string readableSystemName(const char *mangled) {
  std::string name = mangled;
#if defined(__GNUC__) || defined(__clang__)
  int status = 0;
  if (char *demangled =
          abi::__cxa_demangle(mangled, nullptr, nullptr, &status)) {
    if (status == 0)
      name = demangled;
    std::free(demangled);
  }
#endif
  const std::size_t scope = name.rfind("::");
  return scope == std::string::npos ? name : name.substr(scope + 2);
}

} // namespace detail

// Groups run in this order every frame. A system never runs before one in an
// earlier group, so ordering constraints only matter inside a group.
//   PreUpdate:    input, network
//   Simulation:   game logic, physics
//   Presentation: culling, batching, rendering
//   FrameEnd:     bookkeeping that must see the finished frame
enum class SystemGroup { PreUpdate, Simulation, Presentation, FrameEnd };

constexpr size_t NUM_GROUPS = 4;

using SystemID = size_t;

struct SystemRecord {
  std::type_index type;
  SystemGroup group;
  std::shared_ptr<ISystem> system;
  std::string displayName;   // type name without namespace or mangling
  double lastUpdateMs = 0.0; // wall time of the most recent onUpdate
};

/**
 * @brief Registers systems and runs them.
 *
 * Every system belongs to a group. Within a group the order comes from the
 * constraints (addSystemDependencies, addSystemBefore), and registration order
 * breaks ties. Constraints are resolved once, when the systems are created, so
 * they can be added in any order and across plugins. An unknown system, a
 * constraint that points into a later group, or a cycle throws then.
 */
class SystemManager {
public:
  template <typename T> std::shared_ptr<T> registerSystem(SystemGroup group) {
    if (m_built) {
      throw std::logic_error("Systems are already created: register '" +
                             nameOf(typeid(T)) + "' before that");
    }
    if (static_cast<size_t>(group) >= NUM_GROUPS) {
      throw std::runtime_error("Invalid SystemGroup.");
    }
    if (isRegistered<T>()) {
      throw std::runtime_error("System '" + nameOf(typeid(T)) +
                               "' is already registered.");
    }
    auto system = std::make_shared<T>();
    m_systems.push_back(
        {std::type_index(typeid(T)), group, system, nameOf(typeid(T)), 0.0});
    return system;
  }

  /** T runs after each of Ts. */
  template <typename T, typename... Ts> void addSystemDependencies() {
    (m_constraints.push_back(
         {std::type_index(typeid(Ts)), std::type_index(typeid(T))}),
     ...);
  }

  /** T runs before each of Ts. */
  template <typename T, typename... Ts> void addSystemBefore() {
    (m_constraints.push_back(
         {std::type_index(typeid(T)), std::type_index(typeid(Ts))}),
     ...);
  }

  template <typename T> bool isRegistered() const {
    return indexOf(typeid(T)) != kNone;
  }

  /** Fixes the execution order. Throws on a bad constraint. Safe to repeat. */
  void buildExecutionOrder() {
    if (m_built) {
      return;
    }
    const size_t count = m_systems.size();
    std::vector<std::vector<size_t>> after(count);
    std::vector<size_t> waiting(count, 0);

    for (const Constraint &c : m_constraints) {
      const size_t first = indexOf(c.first);
      const size_t second = indexOf(c.second);
      if (first == kNone || second == kNone) {
        throw std::runtime_error(
            "System order: '" + nameOf(first == kNone ? c.first : c.second) +
            "' is not registered (needed by '" +
            nameOf(first == kNone ? c.second : c.first) + "')");
      }
      if (m_systems[first].group > m_systems[second].group) {
        throw std::runtime_error(
            "System order: '" + m_systems[first].displayName +
            "' cannot run before '" + m_systems[second].displayName +
            "', which is in an earlier group");
      }
      if (m_systems[first].group == m_systems[second].group) {
        after[first].push_back(second);
        ++waiting[second];
      }
    }

    // Repeatedly take the earliest-registered system of the earliest group
    // that has nothing left to wait for.
    std::vector<bool> placed(count, false);
    std::vector<SystemRecord> sorted;
    sorted.reserve(count);
    for (size_t round = 0; round < count; ++round) {
      size_t pick = kNone;
      for (size_t i = 0; i < count; ++i) {
        if (placed[i] || waiting[i] != 0) {
          continue;
        }
        if (pick == kNone || m_systems[i].group < m_systems[pick].group) {
          pick = i;
        }
      }
      if (pick == kNone) {
        std::string stuck;
        for (size_t i = 0; i < count; ++i) {
          if (!placed[i]) {
            stuck += (stuck.empty() ? "" : ", ") + m_systems[i].displayName;
          }
        }
        throw std::runtime_error("System order has a cycle among: " + stuck);
      }
      placed[pick] = true;
      for (size_t next : after[pick]) {
        --waiting[next];
      }
      sorted.push_back(m_systems[pick]);
    }
    m_systems = std::move(sorted);
    m_built = true;
  }

  void createAll(World *world) {
    SystemState state{world, 0.0f};
    buildExecutionOrder();
    for (auto const &record : m_systems) {
      LOG_INFO("Creating system: %s", record.displayName.c_str());
      record.system->onCreate(state);
    }
  }

  void updateAll(World *world, float deltaTime) {
    SystemState state{world, deltaTime};
    buildExecutionOrder();
#ifdef COMPONENG_DISABLE_PROFILING
    // The timings only feed the profiling panes; skip the clock reads without
    // them.
    for (auto &record : m_systems)
      record.system->onUpdate(state);
#else
    using Clock = std::chrono::steady_clock;
    for (auto &record : m_systems) {
      const auto start = Clock::now();
      record.system->onUpdate(state);
      record.lastUpdateMs =
          std::chrono::duration<double, std::milli>(Clock::now() - start)
              .count();
    }
#endif
  }

  /** Last created is first destroyed, so a system can rely on the ones it
   *  ran after. */
  void destroyAll(World *world) {
    SystemState state{world, 0.0f};
    buildExecutionOrder();
    for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it) {
      LOG_INFO("Destroying system: %s", it->displayName.c_str());
      it->system->onDestroy(state);
    }
  }

  /** Calls fn(name, lastUpdateMs) for each system, in execution order. */
  template <typename Fn> void forEachSystemTiming(Fn &&fn) const {
    for (const auto &record : m_systems)
      fn(record.displayName.c_str(), record.lastUpdateMs);
  }

private:
  static constexpr size_t kNone = static_cast<size_t>(-1);

  struct Constraint {
    std::type_index first;  // runs first
    std::type_index second; // runs after it
  };

  static std::string nameOf(const std::type_index &type) {
    return detail::readableSystemName(type.name());
  }

  size_t indexOf(const std::type_index &type) const {
    for (size_t i = 0; i < m_systems.size(); ++i) {
      if (m_systems[i].type == type) {
        return i;
      }
    }
    return kNone;
  }

  std::vector<SystemRecord> m_systems;
  std::vector<Constraint> m_constraints;
  bool m_built = false;
};

} // namespace componeng::ecs
