#pragma once

#include "componeng/ecs/archetype/aligned_buffer.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace componeng::ecs {

/**
 * Monotonic per-column change counter. Starts at 0 and only ever grows, so a
 * consumer that remembers the value it last saw can tell whether the column
 * has been written to since by comparing for inequality.
 */
using ColumnVersion = std::uint64_t;

/**
 * @brief A contiguous, type-erased column storing one component type for all
 * entities in an archetype.
 *
 * Components are stored as raw bytes with a known stride (sizeof(T) rounded up
 * to a multiple of alignof(T)). Removal uses swap-remove for O(1) performance,
 * calling the stored destructor callback for non-trivial types to avoid
 * resource leaks.
 *
 * Each column carries a ColumnVersion that is bumped whenever the column is
 * (or may have been) written: when a row is added or removed, and when a
 * writer is handed out through Archetype::writeColumn(). The counter is plain
 * (non-atomic): it is bumped on the thread that starts a write, never from
 * inside a parallel loop body.
 */
class ComponentColumn {
private:
  AlignedBuffer m_buffer;
  void (*m_destructor)(void *) = nullptr;
  ColumnVersion m_version = 0;

public:
  size_t m_stride = 0;
  size_t m_count = 0;

  ComponentColumn(size_t componentSize, size_t alignment,
                  void (*destructor)(void *) = nullptr)
      // Order must match declaration order; members initialize in the latter
      // regardless of how they are listed here.
      : m_buffer(alignment), m_destructor(destructor), m_stride(componentSize) {
    if (m_stride % alignment != 0) {
      m_stride = (m_stride / alignment + 1) * alignment;
    }
  }

  ~ComponentColumn() {
    if (m_destructor) {
      for (size_t i = 0; i < m_count; ++i) {
        m_destructor(at(i));
      }
    }
  }

  ComponentColumn(ComponentColumn &&) = default;
  ComponentColumn &operator=(ComponentColumn &&) = default;

  ComponentColumn(const ComponentColumn &) = delete;
  ComponentColumn &operator=(const ComponentColumn &) = delete;

  void *at(std::size_t index) {
    return m_buffer.data() + index * m_stride;
  }

  const void *at(std::size_t index) const {
    return m_buffer.data() + index * m_stride;
  }

  template <typename T> T &get(size_t row) {
    return *reinterpret_cast<T *>(at(row));
  }

  template <typename T> const T &get(size_t row) const {
    return *reinterpret_cast<const T *>(at(row));
  }

  ColumnVersion version() const {
    return m_version;
  }

  /** Records that the column's contents changed (or may have). */
  void markWritten() {
    ++m_version;
  }

  void pushBackEmpty() {
    if (m_count >= m_buffer.capacity() / m_stride) {
      size_t newCapacity =
          m_buffer.capacity() == 0 ? 8 : m_buffer.capacity() * 2;
      m_buffer.resize(newCapacity * m_stride);
    }
    ++m_count;
    markWritten();
  }

  void remove(size_t row) {
    if (row >= m_count) {
      throw std::runtime_error("ComponentColumn: Invalid row index");
    }
    markWritten();
    if (row < m_count - 1) {
      if (m_destructor)
        m_destructor(at(row));
      std::memcpy(at(row), at(m_count - 1), m_stride);
    } else if (m_destructor) {
      m_destructor(at(row));
    }
    --m_count;
  }
};

} // namespace componeng::ecs
