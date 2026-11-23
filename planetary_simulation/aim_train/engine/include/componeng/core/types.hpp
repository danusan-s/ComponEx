#pragma once

#include "componeng/core/math/ops.hpp"

#include "componeng/core/string_interner.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace componeng::core {

/**
 * @brief Handle to an interned, immutable string.
 *
 * Just an id into a process-wide string table, so it is trivially copyable
 * (archetype storage relocates component rows with memcpy) and has no length
 * limit. Equality and hashing compare ids, never characters.
 *
 * Resolving back to text (c_str/size) takes a lock and is meant for cold
 * paths: serialization, logging, debug UI.
 */
struct Name {
  NameID id = EMPTY_NAME;

  Name() = default;
  Name(const char *s)
      : id(s ? StringInterner::instance().intern(s) : EMPTY_NAME) {
  }
  Name(const std::string &s) : id(StringInterner::instance().intern(s)) {
  }
  Name(std::string_view s) : id(StringInterner::instance().intern(s)) {
  }

  const char *c_str() const {
    return StringInterner::instance().resolve(id);
  }
  bool operator==(const Name &other) const {
    return id == other.id;
  }
  bool operator==(const char *s) const {
    return s && id == StringInterner::instance().intern(s);
  }
  bool operator!=(const Name &other) const {
    return !(*this == other);
  }
  bool operator!=(const char *s) const {
    return !(*this == s);
  }
  operator const char *() const {
    return c_str();
  }
  operator std::string() const {
    return c_str();
  }
  bool empty() const {
    return id == EMPTY_NAME;
  }
  std::size_t size() const {
    return StringInterner::instance().length(id);
  }
};

static_assert(std::is_trivially_copyable_v<Name>,
              "Name must stay memcpy-relocatable for archetype storage");

} // namespace componeng::core

namespace std {
template <> struct hash<componeng::core::Name> {
  std::size_t operator()(const componeng::core::Name &name) const noexcept {
    return std::hash<componeng::core::NameID>{}(name.id);
  }
};
} // namespace std

namespace componeng::core {

using HandleID = uint32_t;
constexpr HandleID INVALID_HANDLE = UINT32_MAX;

using UniformMap = std::unordered_map<
    Name, std::variant<float, core::Vec2, core::Vec3, core::Vec4, core::Mat4>>;

} // namespace componeng::core
