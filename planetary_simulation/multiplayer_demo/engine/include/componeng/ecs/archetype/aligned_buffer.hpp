#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

namespace componeng::ecs {

/**
 * @brief A growable, aligned block of raw bytes.
 *
 * Allocates memory via the aligned operator new to guarantee proper alignment
 * for extended-alignment types. Supports manual growth, move semantics, and
 * disallows copies.
 */
class AlignedBuffer {
private:
  std::byte *m_data = nullptr;
  size_t m_size = 0;
  size_t m_capacity = 0;
  size_t m_alignment = 0;

  // Not std::aligned_alloc: Windows has no such function.
  void release() {
    if (m_data) {
      ::operator delete(m_data, std::align_val_t(m_alignment));
      m_data = nullptr;
    }
  }

public:
  explicit AlignedBuffer(size_t alignment)
      : m_alignment(std::max(alignment, sizeof(void *))) {
  }

  ~AlignedBuffer() {
    release();
  }

  AlignedBuffer(AlignedBuffer &&other) noexcept
      : m_data(other.m_data), m_size(other.m_size),
        m_capacity(other.m_capacity), m_alignment(other.m_alignment) {
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_capacity = 0;
  }

  AlignedBuffer &operator=(AlignedBuffer &&other) noexcept {
    if (this != &other) {
      release();
      m_data = other.m_data;
      m_size = other.m_size;
      m_capacity = other.m_capacity;
      m_alignment = other.m_alignment;
      other.m_data = nullptr;
      other.m_size = 0;
      other.m_capacity = 0;
    }
    return *this;
  }

  AlignedBuffer(const AlignedBuffer &) = delete;
  AlignedBuffer &operator=(const AlignedBuffer &) = delete;

  void reserve(size_t newCapacity) {
    if (newCapacity <= m_capacity)
      return;
    size_t allocSize = newCapacity;
    if (allocSize % m_alignment != 0) {
      allocSize = (allocSize / m_alignment + 1) * m_alignment;
    }
    std::byte *newData = static_cast<std::byte *>(
        ::operator new(allocSize, std::align_val_t(m_alignment)));
    if (m_data) {
      std::memcpy(newData, m_data, m_size);
      release();
    }
    m_data = newData;
    m_capacity = newCapacity;
  }

  void resize(size_t newSize) {
    reserve(newSize);
    m_size = newSize;
  }

  void append(const void *src, size_t count) {
    size_t oldSize = m_size;
    resize(m_size + count);
    if (src) {
      std::memcpy(m_data + oldSize, src, count);
    }
  }

  std::byte *data() {
    return m_data;
  }
  const std::byte *data() const {
    return m_data;
  }
  size_t size() const {
    return m_size;
  }
  size_t capacity() const {
    return m_capacity;
  }
};

} // namespace componeng::ecs
