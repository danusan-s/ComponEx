#pragma once

#include "componeng/ecs/archetype/archetype.hpp"
#include "componeng/ecs/component_registry.hpp"
#include "componeng/ecs/entity.hpp"
#include "componeng/ecs/thread_pool.hpp"

#include <algorithm>
#include <future>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace componeng::ecs {

struct QueryDesc {
  Signature required;
  Signature excluded;
};

/**
 * @brief Read access to component T within a Query.
 *
 * The callback receives `const T &`. Fetching the column leaves its version
 * untouched.
 */
template <typename T> struct Read {
  using Component = T;
  using Ref = const T &;
  using ColumnPtr = const ComponentColumn *;

  static ColumnPtr column(Archetype &archetype, ComponentID id) {
    return &archetype.readColumn(id);
  }
  static Ref at(ColumnPtr column, std::size_t row) {
    return column->template get<T>(row);
  }
};

/**
 * @brief Write access to component T within a Query.
 *
 * The callback receives `T &`. Fetching the column bumps its version, once
 * per archetype per query run, regardless of how many rows are visited.
 */
template <typename T> struct Write {
  using Component = T;
  using Ref = T &;
  using ColumnPtr = ComponentColumn *;

  static ColumnPtr column(Archetype &archetype, ComponentID id) {
    return &archetype.writeColumn(id);
  }
  static Ref at(ColumnPtr column, std::size_t row) {
    return column->template get<T>(row);
  }
};

namespace detail {

template <typename...> struct AllDistinct : std::true_type {};
template <typename T, typename... Rest>
struct AllDistinct<T, Rest...>
    : std::bool_constant<(!std::is_same_v<T, Rest> && ...) &&
                         AllDistinct<Rest...>::value> {};

/** Index of the Access whose Component is T, or sizeof...(A) if none. */
template <typename T, typename... A> constexpr std::size_t accessIndex() {
  constexpr bool matches[] = {std::is_same_v<T, typename A::Component>...};
  for (std::size_t i = 0; i < sizeof...(A); ++i) {
    if (matches[i])
      return i;
  }
  return sizeof...(A);
}

} // namespace detail

/**
 * @brief One archetype's slice of a query: its rows, as raw arrays.
 *
 * Handed to eachChunk() / eachChunkParallel() callbacks instead of one entity
 * at a time. It exposes what only the archetype knows: how many rows it has,
 * the current version of a component's column, and each declared component as
 * a contiguous array indexed by row. That is what lets a cache keyed on a
 * column version skip a whole archetype with one compare.
 *
 * A chunk is a window [begin(), end()) onto the archetype's rows. eachChunk()
 * hands out the full archetype; eachChunkParallel() hands each task a
 * sub-range. size() is always the archetype's total row count, and data<T>()
 * is always indexed by absolute row, so loop `for (row = begin(); row < end();
 * ++row)`.
 */
template <typename... Access> class QueryChunk {
  template <typename...> friend class Query;

private:
  using Columns = std::tuple<typename Access::ColumnPtr...>;

  const Archetype *m_archetype;
  ComponentRegistry *m_registry;
  ArchetypeID m_id;
  Columns m_columns;
  std::size_t m_begin;
  std::size_t m_end;

  QueryChunk(const Archetype &archetype, ComponentRegistry &registry,
             ArchetypeID id, Columns columns, std::size_t begin,
             std::size_t end)
      : m_archetype(&archetype), m_registry(&registry), m_id(id),
        m_columns(columns), m_begin(begin), m_end(end) {
  }

public:
  /** Stable for the lifetime of the World: archetypes are never removed. */
  ArchetypeID archetypeId() const {
    return m_id;
  }

  /** Total rows in the archetype (not the width of this chunk's window). */
  std::size_t size() const {
    return m_archetype->getEntityCount();
  }

  std::size_t begin() const {
    return m_begin;
  }

  std::size_t end() const {
    return m_end;
  }

  EntityID entity(std::size_t row) const {
    return m_archetype->getEntityForRow(row);
  }

  /** Current version of T's column. T must be a component of the archetype. */
  template <typename T> ColumnVersion version() const {
    return m_archetype->columnVersion(m_registry->template getComponentID<T>());
  }

  /**
   * T's column as an array indexed by row: `const T *` if T was declared
   * read<>, `T *` if write<>. T must have been declared in the query.
   */
  template <typename T> auto data() const {
    constexpr std::size_t I = detail::accessIndex<T, Access...>();
    static_assert(I < sizeof...(Access),
                  "data<T>() needs T declared with read<>() or write<>()");
    using Ref = typename std::tuple_element_t<I, std::tuple<Access...>>::Ref;
    using Ptr = std::remove_reference_t<Ref> *;
    return reinterpret_cast<Ptr>(std::get<I>(m_columns)->at(0));
  }
};

/**
 * @brief Forward iterator over a Query, for range-for.
 *
 * Dereferences to a tuple of `const T &` / `T &` in declaration order. Write
 * columns are versioned when iteration enters an archetype, so an archetype
 * that is never reached is never marked written.
 */
template <typename... Access> class QueryIterator {
private:
  using Columns = std::tuple<typename Access::ColumnPtr...>;

  const std::vector<Archetype *> *m_archetypes;
  ComponentRegistry *m_registry;
  size_t m_archetypeIndex = 0;
  size_t m_rowIndex = 0;
  Columns m_columns{};

  // Skips empty archetypes and fetches the columns of the one it lands on.
  void enterArchetype() {
    while (m_archetypeIndex < m_archetypes->size() &&
           (*m_archetypes)[m_archetypeIndex]->getEntityCount() == 0) {
      ++m_archetypeIndex;
    }
    if (m_archetypeIndex >= m_archetypes->size())
      return;

    Archetype &archetype = *(*m_archetypes)[m_archetypeIndex];
    m_columns = Columns{Access::column(
        archetype,
        m_registry->template getComponentID<typename Access::Component>())...};
  }

  template <size_t... I> auto deref(std::index_sequence<I...>) const {
    return std::tuple<typename Access::Ref...>(
        Access::at(std::get<I>(m_columns), m_rowIndex)...);
  }

public:
  QueryIterator(const std::vector<Archetype *> &archetypes,
                ComponentRegistry &registry, size_t archetypeIndex = 0)
      : m_archetypes(&archetypes), m_registry(&registry),
        m_archetypeIndex(archetypeIndex) {
    enterArchetype();
  }

  QueryIterator &operator++() {
    if (m_archetypeIndex >= m_archetypes->size())
      return *this;

    ++m_rowIndex;
    if (m_rowIndex >= (*m_archetypes)[m_archetypeIndex]->getEntityCount()) {
      ++m_archetypeIndex;
      m_rowIndex = 0;
      enterArchetype();
    }
    return *this;
  }

  QueryIterator operator++(int) {
    QueryIterator tmp = *this;
    ++(*this);
    return tmp;
  }

  bool operator==(const QueryIterator &other) const {
    return m_archetypeIndex == other.m_archetypeIndex &&
           m_rowIndex == other.m_rowIndex;
  }

  bool operator!=(const QueryIterator &other) const {
    return !(*this == other);
  }

  std::tuple<typename Access::Ref...> operator*() const {
    return deref(std::index_sequence_for<Access...>{});
  }
};

/**
 * @brief Typed query over entities matching a set of declared accesses.
 *
 * Built from World::query() by chaining read<>() and write<>():
 *
 *   world.query().read<Mesh>().write<Transform>().each(
 *       [](const Mesh &m, Transform &t) { ... });
 *
 * Each Access is Read<T> or Write<T>. An entity matches an archetype that
 * holds every declared component and none of the exclude<>()d ones. Callback
 * parameters follow declaration order: `const T &` for read, `T &` for write.
 *
 * Write columns have their version bumped once per matching, non-empty
 * archetype each time the query runs (each / eachWithEntity / eachParallel /
 * entering an archetype during range-for). Read columns are left alone. The
 * bump happens up front, on the calling thread, so parallel bodies never
 * touch the counter.
 *
 * A component can be declared only once; reading and writing the same type in
 * one query is a compile error -- declare it write.
 *
 * Also supports exclude() to filter out entities with specific components,
 * and eachParallel() to distribute work across the engine's ThreadPool.
 *
 * @tparam Access... Read<T> / Write<T> entries, in declaration order.
 */
template <typename... Access> class Query {
  static_assert(detail::AllDistinct<typename Access::Component...>::value,
                "A component may appear only once in a query; to read and "
                "write the same component, declare it with write<>()");

  template <typename...> friend class Query;

private:
  using Columns = std::tuple<typename Access::ColumnPtr...>;

  std::array<Archetype, MAX_ARCHETYPES> &m_archetypes;
  std::size_t m_archetypeCount = 0;
  std::vector<Archetype *> m_matchingArchetypes;
  bool m_dirty = true;
  ComponentRegistry &m_registry;
  QueryDesc desc;

  Query(std::array<Archetype, MAX_ARCHETYPES> &archetypes,
        std::size_t archetypeCount, ComponentRegistry &registry, QueryDesc d)
      : m_archetypes(archetypes), m_archetypeCount(archetypeCount),
        m_registry(registry), desc(d) {
  }

  ArchetypeID idOf(const Archetype &archetype) const {
    return static_cast<ArchetypeID>(&archetype - m_archetypes.data());
  }

  /** Fetches this query's columns for an archetype, versioning write ones. */
  Columns fetchColumns(Archetype &archetype) {
    return Columns{Access::column(
        archetype,
        m_registry.template getComponentID<typename Access::Component>())...};
  }

  template <typename Fn, size_t... I>
  static void invokeCallback(Fn &fn, const Columns &cols, size_t row,
                             std::index_sequence<I...>) {
    fn(Access::at(std::get<I>(cols), row)...);
  }

  template <typename Fn, size_t... I>
  static void invokeCallbackWithEntity(Fn &fn, EntityID entity,
                                       const Columns &cols, size_t row,
                                       std::index_sequence<I...>) {
    fn(entity, Access::at(std::get<I>(cols), row)...);
  }

public:
  bool matches(const Archetype &archetype) {
    return (archetype.m_signature & desc.required) == desc.required &&
           (archetype.m_signature & desc.excluded).none();
  }

  void updateMatchingArchetypes() {
    if (!m_dirty)
      return;

    m_matchingArchetypes.clear();
    // Only [0, m_archetypeCount) has been initialised; scanning the whole
    // MAX_ARCHETYPES array would cost the ceiling rather than the actual
    // number of archetypes, on every query.
    for (std::size_t i = 0; i < m_archetypeCount; ++i) {
      if (matches(m_archetypes[i])) {
        m_matchingArchetypes.push_back(&m_archetypes[i]);
      }
    }
    m_dirty = false;
  }

  Query(std::array<Archetype, MAX_ARCHETYPES> &archetypes,
        std::size_t archetypeCount, ComponentRegistry &registry)
      : m_archetypes(archetypes), m_archetypeCount(archetypeCount),
        m_registry(registry) {
    desc.required = m_registry.makeSignature<typename Access::Component...>();
  }

  /** Adds read-only components to the query. */
  template <typename... Ts> Query<Access..., Read<Ts>...> read() const {
    QueryDesc next = desc;
    next.required |= m_registry.makeSignature<Ts...>();
    return Query<Access..., Read<Ts>...>(m_archetypes, m_archetypeCount,
                                         m_registry, next);
  }

  /** Adds components the query may modify. Marks their columns written. */
  template <typename... Ts> Query<Access..., Write<Ts>...> write() const {
    QueryDesc next = desc;
    next.required |= m_registry.makeSignature<Ts...>();
    return Query<Access..., Write<Ts>...>(m_archetypes, m_archetypeCount,
                                          m_registry, next);
  }

  /** Skips archetypes holding any of Excl. Not part of the callback. */
  template <typename... Excl> Query &exclude() {
    desc.excluded |= m_registry.makeSignature<Excl...>();
    return *this;
  }

  template <typename Fn> void each(Fn fn) {
    static_assert(sizeof...(Access) > 0,
                  "Declare components with read<>() / write<>() before each()");
    updateMatchingArchetypes();

    for (Archetype *archetype : m_matchingArchetypes) {
      const size_t n = archetype->getEntityCount();
      if (n == 0)
        continue;

      const Columns cols = fetchColumns(*archetype);
      for (size_t i = 0; i < n; ++i)
        invokeCallback(fn, cols, i, std::index_sequence_for<Access...>{});
    }
  }

  template <typename Fn> void eachWithEntity(Fn fn) {
    static_assert(sizeof...(Access) > 0,
                  "Declare components with read<>() / write<>() before "
                  "eachWithEntity()");
    updateMatchingArchetypes();

    for (Archetype *archetype : m_matchingArchetypes) {
      const size_t n = archetype->getEntityCount();
      if (n == 0)
        continue;

      const Columns cols = fetchColumns(*archetype);
      for (size_t i = 0; i < n; ++i)
        invokeCallbackWithEntity(fn, archetype->getEntityForRow(i), cols, i,
                                 std::index_sequence_for<Access...>{});
    }
  }

  /**
   * Calls fn once per matching non-empty archetype with its QueryChunk.
   * Write columns are versioned once per archetype, as with each().
   */
  template <typename Fn> void eachChunk(Fn fn) {
    static_assert(sizeof...(Access) > 0,
                  "Declare components with read<>() / write<>() before "
                  "eachChunk()");
    updateMatchingArchetypes();

    for (Archetype *archetype : m_matchingArchetypes) {
      const size_t n = archetype->getEntityCount();
      if (n == 0)
        continue;

      const QueryChunk<Access...> chunk(*archetype, m_registry,
                                        idOf(*archetype),
                                        fetchColumns(*archetype), 0, n);
      fn(chunk);
    }
  }

  /**
   * Like eachChunk(), but large archetypes are split into row ranges that run
   * on the pool, each task getting a chunk whose [begin(), end()) is its
   * share. Small archetypes run inline as one chunk. Columns are fetched (and
   * versioned) here on the calling thread before any task starts; fn must be
   * safe to call concurrently on disjoint row ranges.
   */
  template <typename Fn> void eachChunkParallel(ThreadPool &pool, Fn fn) {
    static_assert(sizeof...(Access) > 0,
                  "Declare components with read<>() / write<>() before "
                  "eachChunkParallel()");
    std::vector<std::future<void>> futures;
    updateMatchingArchetypes();

    for (Archetype *archetype : m_matchingArchetypes) {
      const size_t n = archetype->getEntityCount();
      if (n == 0)
        continue;

      constexpr size_t minChunkSize = 50;

      const Columns cols = fetchColumns(*archetype);
      const ArchetypeID id = idOf(*archetype);

      // If less than 50 running non parallel is faster as there is a
      // overhead when we create task and then the pool unlocks and runs
      // it. For small number of entities, that overhead is more than the
      // time it takes to just run the loop in the current thread.
      if (n < minChunkSize) {
        fn(QueryChunk<Access...>(*archetype, m_registry, id, cols, 0, n));
        continue;
      }

      const size_t maxChunks = (n + minChunkSize - 1) / minChunkSize;
      const size_t numThreads = std::min(pool.threadCount(), maxChunks);
      const size_t chunkSize = n / numThreads;

      for (size_t c = 0; c < numThreads; ++c) {
        const size_t start = c * chunkSize;
        const size_t end = (c == numThreads - 1) ? n : start + chunkSize;

        // The task gets its own copy of fn, and the chunk (with its column
        // pointers) is built inside it: tasks outlive this iteration.
        futures.push_back(
            pool.submit([this, archetype, id, cols, start, end, fn]() mutable {
              fn(QueryChunk<Access...>(*archetype, m_registry, id, cols, start,
                                       end));
            }));
      }
    }

    for (auto &f : futures)
      f.wait();
  }

  template <typename Fn> void eachParallel(ThreadPool &pool, Fn fn) {
    static_assert(sizeof...(Access) > 0,
                  "Declare components with read<>() / write<>() before "
                  "eachParallel()");
    eachChunkParallel(pool, [fn](const QueryChunk<Access...> &chunk) mutable {
      for (size_t i = chunk.begin(); i < chunk.end(); ++i) {
        invokeCallback(fn, chunk.m_columns, i,
                       std::index_sequence_for<Access...>{});
      }
    });
  }

  QueryIterator<Access...> begin() {
    updateMatchingArchetypes();
    return QueryIterator<Access...>(m_matchingArchetypes, m_registry);
  }

  QueryIterator<Access...> end() {
    updateMatchingArchetypes();
    return QueryIterator<Access...>(m_matchingArchetypes, m_registry,
                                    m_matchingArchetypes.size());
  }
};

} // namespace componeng::ecs
