#pragma once

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace componeng::ecs {

struct IResource {
  virtual ~IResource() = default;
};

template <typename T> struct ResourceHolder : IResource {
  // Constructed in place, so a resource only needs to be movable: it does not
  // have to be default-constructible (e.g. one that holds a reference).
  template <typename U>
  explicit ResourceHolder(U &&initial) : value(std::forward<U>(initial)) {
  }
  T value;
};

class ResourceManager {
public:
  template <typename T> void set(T &&value) {
    using Stored = std::remove_cv_t<std::remove_reference_t<T>>;
    m_resources[std::type_index(typeid(Stored))] =
        std::make_unique<ResourceHolder<Stored>>(std::forward<T>(value));
  }

  template <typename T> T &get() {
    auto it = m_resources.find(std::type_index(typeid(T)));
    if (it == m_resources.end()) {
      throw std::runtime_error("Resource not found");
    }
    return static_cast<ResourceHolder<T> *>(it->second.get())->value;
  }

  template <typename T> bool contains() const {
    return m_resources.find(std::type_index(typeid(T))) != m_resources.end();
  }

private:
  std::unordered_map<std::type_index, std::unique_ptr<IResource>> m_resources;
};

} // namespace componeng::ecs
