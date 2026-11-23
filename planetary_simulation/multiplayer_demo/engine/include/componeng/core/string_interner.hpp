#pragma once

#include <cstdint>
#include <string_view>

namespace componeng::core {

using NameID = std::uint32_t;

inline constexpr NameID EMPTY_NAME = 0;

/**
 * @brief Process-wide table of interned, immutable strings backing core::Name.
 *
 * Single instance by design: an id only means anything relative to the table
 * that issued it, so a second table would silently reinterpret ids. Entries
 * are append-only and never removed.
 *
 * Holds no members -- the table lives in the .cpp -- so this header stays
 * cheap to include. types.hpp pulls it into effectively every translation
 * unit, and declaring the containers here would drag <deque>, <unordered_map>
 * and <shared_mutex> along with it.
 */
class StringInterner {
public:
  static StringInterner &instance();

  StringInterner(const StringInterner &) = delete;
  StringInterner &operator=(const StringInterner &) = delete;
  StringInterner(StringInterner &&) = delete;
  StringInterner &operator=(StringInterner &&) = delete;

  // Thread-safe. Returns the existing id if this text was already interned.
  NameID intern(std::string_view s);

  // Cold path only (serialization, logging, debug UI): comparing or hashing
  const char *resolve(NameID id) const;
  std::size_t length(NameID id) const;

  // Number of distinct strings interned so far; for diagnostics.
  std::size_t count() const;

private:
  StringInterner() = default;
  ~StringInterner() = default;
};

} // namespace componeng::core
