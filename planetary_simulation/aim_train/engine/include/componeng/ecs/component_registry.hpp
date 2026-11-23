#pragma once

#include "componeng/ecs/component_serializer.hpp"
#include "entity.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace componeng::ecs {

namespace detail {

/**
 * @brief A small integer unique to a C++ type, the same in every registry.
 *
 * Handed out once per type, on first use, from a process-wide counter, then
 * read back from a function-local static, so asking for it costs one branch
 * on an already-initialised guard. This is what lets ComponentRegistry find
 * a type's ComponentID by indexing an array instead of hashing the type's
 * name on every lookup (std::type_index hashes the mangled name's characters).
 *
 * It is deliberately NOT the ComponentID. A ComponentID is per registry (two
 * worlds may register the same types in different orders and get different
 * ids); this index only says "which type is this", and each registry keeps
 * its own table from it to its own id.
 */
inline std::size_t nextComponentTypeIndex() {
  static std::atomic<std::size_t> counter{0};
  return counter.fetch_add(1, std::memory_order_relaxed);
}

// The static lives here, in a function templated on the *plain* type, so there
// is exactly one index per type however it is spelled.
template <typename Plain> std::size_t plainComponentTypeIndex() {
  static const std::size_t index = nextComponentTypeIndex();
  return index;
}

// T, const T and T& share an index, matching what typeid() ignores.
template <typename T> std::size_t componentTypeIndex() {
  return plainComponentTypeIndex<
      std::remove_cv_t<std::remove_reference_t<T>>>();
}

} // namespace detail

/**
 * @brief Runtime metadata associated with a registered component type.
 *
 * Stores the type name, size in bytes, and an optional destructor
 * callback for non-trivially-destructible types.
 */
struct ComponentInfo {
  const char *name = nullptr;
  size_t size = 0;
  size_t alignment = 0;
  void (*destructor)(void *) = nullptr;
  nlohmann::json (*serializer)(const void *) = nullptr;
  void *(*deserializer)(const nlohmann::json &) = nullptr;
  /**
   * Frees the heap object returned by deserializer.
   *
   * Callers typically memcpy the deserialized bytes into archetype storage
   * (see World::addComponentById) and then free the source. That is only
   * correct because every component type is trivially copyable — which is
   * why core::Name exists instead of std::string. A component with an owning
   * member would turn this into a double-free.
   */
  void (*deleter)(void *) = nullptr;
};

/**
 * @brief Maps types to compact ComponentIDs at runtime.
 *
 * Each distinct component type is assigned a unique ID the first time
 * it is registered. These IDs are used as bit indices in Signatures
 * and as column indices within archetypes.
 */
class ComponentRegistry {
private:
  static constexpr ComponentID kUnregistered =
      std::numeric_limits<ComponentID>::max();

  // This registry's ComponentID for each type, indexed by
  // detail::componentTypeIndex<T>(); kUnregistered where the type has not been
  // registered here. Written during registration, then only read, so queries
  // running on worker threads can use it without locking.
  std::vector<ComponentID> m_idByTypeIndex;
  std::unordered_map<std::string_view, ComponentID> m_nameToID;
  std::array<ComponentInfo, MAX_COMPONENTS> m_componentInfos;
  ComponentID m_nextComponentID = 0;

public:
  template <typename T> ComponentID registerComponent() {
    static_assert(!std::is_pointer_v<T>,
                  "Component type must not be a pointer");
    static_assert(!std::is_reference_v<T>,
                  "Component type must not be a reference");
    static_assert(std::is_move_constructible_v<T>,
                  "Component must be move-constructible");

    constexpr const char *typeName = T::component_name;
    static_assert(
        typeName != nullptr,
        "Component type is missing a component_name. "
        "Add 'static constexpr const char* component_name = \"...\";' "
        "to the component struct.");

    const std::size_t typeIndex = detail::componentTypeIndex<T>();
    if (typeIndex < m_idByTypeIndex.size() &&
        m_idByTypeIndex[typeIndex] != kUnregistered)
      return m_idByTypeIndex[typeIndex];

    if (m_nextComponentID >= MAX_COMPONENTS)
      throw std::runtime_error("ComponentRegistry: MAX_COMPONENTS exceeded");

    auto nameIt = m_nameToID.find(typeName);
    if (nameIt != m_nameToID.end()) {
      throw std::runtime_error(
          std::string("Duplicate component name '") + typeName +
          "' — two different component types cannot share the same name");
    }

    ComponentID id = m_nextComponentID++;
    if (typeIndex >= m_idByTypeIndex.size())
      m_idByTypeIndex.resize(typeIndex + 1, kUnregistered);
    m_idByTypeIndex[typeIndex] = id;
    m_componentInfos[id].name = typeName;
    m_nameToID[typeName] = id;
    m_componentInfos[id].size = sizeof(T);
    m_componentInfos[id].alignment = alignof(T);
    // Store destructor for non-trivial types so archetype moves can clean up.
    if constexpr (!std::is_trivially_destructible_v<T>) {
      m_componentInfos[id].destructor = [](void *ptr) {
        static_cast<T *>(ptr)->~T();
      };
    }
    // Store serializer and deserializer if specialization exists for this type.
    if constexpr (std::is_base_of_v<ComponentSerializer<T>,
                                    ComponentSerializer<T>>) {
      m_componentInfos[id].serializer =
          [](const void *componentPtr) -> nlohmann::json {
        const T &component = *static_cast<const T *>(componentPtr);
        return ComponentSerializer<T>::serialize(component);
      };
      m_componentInfos[id].deserializer =
          [](const nlohmann::json &json) -> void * {
        T component = ComponentSerializer<T>::deserialize(json);
        return new T(std::move(component));
      };
      m_componentInfos[id].deleter = [](void *ptr) {
        delete static_cast<T *>(ptr);
      };
    }

    return id;
  }

  ComponentID getComponentIDByName(const std::string_view &name) const {
    auto it = m_nameToID.find(name);
    if (it == m_nameToID.end())
      throw std::runtime_error("ComponentRegistry: unknown component name");
    return it->second;
  }

  /**
   * ComponentID of an already-registered type. Hot: World::getComponent and
   * every query call this per entity, so it is an array index, not a hash
   * lookup. Throws std::runtime_error for a type that was never registered
   * with this registry.
   */
  template <typename T> ComponentID getComponentID() const {
    const std::size_t typeIndex = detail::componentTypeIndex<T>();
    if (typeIndex >= m_idByTypeIndex.size() ||
        m_idByTypeIndex[typeIndex] == kUnregistered)
      throwUnregistered(); // [[noreturn]], so the compiler treats it as cold
    return m_idByTypeIndex[typeIndex];
  }

  ComponentID getComponentCount() const {
    return m_nextComponentID;
  }

  ComponentInfo &getComponentInfo(ComponentID id) {
    if (id >= m_nextComponentID)
      throw std::runtime_error("ComponentRegistry: invalid ComponentID");
    return m_componentInfos[id];
  }

  [[noreturn]] static void throwUnregistered() {
    throw std::runtime_error("ComponentRegistry: unregistered component type");
  }

  template <typename... Ts> Signature makeSignature() const {
    Signature sig;
    (sig.set(getComponentID<Ts>()), ...);
    return sig;
  }
};

} // namespace componeng::ecs
