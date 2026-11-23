#include "componeng/core/string_interner.hpp"

#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace componeng::core {

// Cool trick I learned that basically does the same as static
namespace {

struct Table {
  // deque over vector cuz vector reallocs invalidate string_views
  std::deque<std::string> entries;
  std::unordered_map<std::string_view, NameID> lookup;
  mutable std::shared_mutex mutex;

  Table() {
    entries.emplace_back();
    lookup.emplace(std::string_view(entries.front()), EMPTY_NAME);
  }
};

// Singleton table instance, lazily initialized on first use.
Table &table() {
  static Table *instance = new Table();
  return *instance;
}

} // namespace

StringInterner &StringInterner::instance() {
  static StringInterner interner;
  return interner;
}

NameID StringInterner::intern(std::string_view s) {
  Table &self = table();

  {
    std::shared_lock<std::shared_mutex> read(self.mutex);
    auto it = self.lookup.find(s);
    if (it != self.lookup.end()) {
      return it->second;
    }
  }

  std::unique_lock<std::shared_mutex> write(self.mutex);
  // Re-check: another thread may have interned this between the two locks.
  auto it = self.lookup.find(s);
  if (it != self.lookup.end()) {
    return it->second;
  }

  const NameID id = static_cast<NameID>(self.entries.size());
  self.entries.emplace_back(s);
  // Key must view the stored copy, not the caller's buffer.
  self.lookup.emplace(std::string_view(self.entries.back()), id);
  return id;
}

const char *StringInterner::resolve(NameID id) const {
  Table &self = table();
  std::shared_lock<std::shared_mutex> read(self.mutex);
  if (id >= self.entries.size()) {
    return "";
  }
  return self.entries[id].c_str();
}

std::size_t StringInterner::length(NameID id) const {
  Table &self = table();
  std::shared_lock<std::shared_mutex> read(self.mutex);
  if (id >= self.entries.size()) {
    return 0;
  }
  return self.entries[id].size();
}

std::size_t StringInterner::count() const {
  Table &self = table();
  std::shared_lock<std::shared_mutex> read(self.mutex);
  return self.entries.size();
}

} // namespace componeng::core
