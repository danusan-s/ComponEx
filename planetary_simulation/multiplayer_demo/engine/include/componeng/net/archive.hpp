#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace componeng::net {

// Turns values into bytes and back, identically on every platform. A type is
// described once, by a serialize() that serves both directions:
//
//   template <typename Ar> void serialize(Ar &ar) { ar(from, to, note); }
//
// A type you cannot edit gets a free serialize(Ar &, T &). Supported: bool,
// integers (use fixed-width types, not long/size_t), enums, float/double
// (little endian), std::string/vector/array (varint lengths) and nested types.
//
// Input is untrusted: a bad read makes the reader failed(), after which it
// yields zeros and never allocates more than the input could hold. Check
// failed() once per message; whether values make sense is the game's job.

namespace detail {

template <typename T, typename Ar, typename = void>
struct HasMemberSerialize : std::false_type {};
template <typename T, typename Ar>
struct HasMemberSerialize<
    T, Ar,
    std::void_t<decltype(std::declval<T &>().serialize(std::declval<Ar &>()))>>
    : std::true_type {};

template <typename T, typename Ar, typename = void>
struct HasFreeSerialize : std::false_type {};
template <typename T, typename Ar>
struct HasFreeSerialize<
    T, Ar,
    std::void_t<decltype(serialize(std::declval<Ar &>(), std::declval<T &>()))>>
    : std::true_type {};

template <typename T> struct IsVector : std::false_type {};
template <typename T, typename A>
struct IsVector<std::vector<T, A>> : std::true_type {};

template <typename T> struct IsStdArray : std::false_type {};
template <typename T, size_t N>
struct IsStdArray<std::array<T, N>> : std::true_type {};

template <typename T>
constexpr bool kIsByte = std::is_same_v<T, uint8_t> ||
                         std::is_same_v<T, int8_t> || std::is_same_v<T, char>;

// Layout of each kind of value, shared by writer and reader; `Ar` supplies the
// primitives and kReading.
template <typename Ar, typename T> void archiveValue(Ar &ar, T &value) {
  constexpr bool reading = Ar::kReading;

  if constexpr (std::is_same_v<T, bool>) {
    uint64_t raw = value ? 1 : 0;
    ar.fixed(raw, 1);
    if constexpr (reading) {
      if (raw > 1) {
        ar.fail();
      }
      value = raw == 1;
    }
  } else if constexpr (std::is_integral_v<T>) {
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                      sizeof(T) == 8,
                  "unsupported integer size");
    uint64_t raw = static_cast<uint64_t>(value);
    ar.fixed(raw, sizeof(T));
    if constexpr (reading) {
      value = static_cast<T>(raw);
    }
  } else if constexpr (std::is_enum_v<T>) {
    auto underlying = static_cast<std::underlying_type_t<T>>(value);
    archiveValue(ar, underlying);
    if constexpr (reading) {
      value = static_cast<T>(underlying);
    }
  } else if constexpr (std::is_floating_point_v<T>) {
    static_assert(std::numeric_limits<T>::is_iec559 &&
                      (sizeof(T) == 4 || sizeof(T) == 8),
                  "floats must be IEEE 754 single or double");
    using Bits = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
    Bits bits;
    std::memcpy(&bits, &value, sizeof(T));
    uint64_t raw = bits;
    ar.fixed(raw, sizeof(T));
    if constexpr (reading) {
      bits = static_cast<Bits>(raw);
      std::memcpy(&value, &bits, sizeof(T));
    }
  } else if constexpr (std::is_same_v<T, std::string>) {
    uint64_t length = value.size();
    ar.length(length);
    if constexpr (reading) {
      if (!ar.canRead(length)) {
        value.clear();
        return;
      }
      value.resize(static_cast<size_t>(length));
    }
    ar.bytes(value.data(), static_cast<size_t>(length));
  } else if constexpr (IsVector<T>::value) {
    using Element = typename T::value_type;
    static_assert(!std::is_same_v<Element, bool>,
                  "std::vector<bool> is not supported; use uint8_t");
    uint64_t count = value.size();
    ar.length(count);
    if constexpr (reading) {
      // Each element takes at least a byte, so a bigger count is a lie;
      // refusing it stops forged allocations.
      if (!ar.canRead(count)) {
        value.clear();
        return;
      }
      value.clear();
      value.resize(static_cast<size_t>(count));
    }
    if constexpr (kIsByte<Element>) {
      ar.bytes(value.data(), static_cast<size_t>(count));
    } else {
      for (Element &element : value) {
        archiveValue(ar, element);
        if (ar.failed()) {
          break;
        }
      }
    }
  } else if constexpr (IsStdArray<T>::value) {
    for (auto &element : value) {
      archiveValue(ar, element);
    }
  } else if constexpr (HasMemberSerialize<T, Ar>::value) {
    value.serialize(ar);
  } else if constexpr (HasFreeSerialize<T, Ar>::value) {
    serialize(ar, value);
  } else {
    static_assert(HasMemberSerialize<T, Ar>::value,
                  "type has no serialize(Ar &) member or free serialize(Ar &, "
                  "T &) function");
  }
}

} // namespace detail

class ByteWriter {
public:
  static constexpr bool kReading = false;

  ByteWriter() = default;
  explicit ByteWriter(size_t reserve) {
    m_data.reserve(reserve);
  }

  // Values are only read; the const_cast lets one serialize() serve both
  // directions.
  template <typename... Ts> ByteWriter &operator()(const Ts &...values) {
    (detail::archiveValue(*this, const_cast<Ts &>(values)), ...);
    return *this;
  }

  const std::vector<uint8_t> &data() const {
    return m_data;
  }
  size_t size() const {
    return m_data.size();
  }
  std::vector<uint8_t> release() {
    return std::move(m_data);
  }
  void clear() {
    m_data.clear();
  }
  bool failed() const {
    return m_failed;
  }

  void fixed(uint64_t &value, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      m_data.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
  }
  void length(uint64_t &value) {
    if (value > std::numeric_limits<uint32_t>::max()) {
      m_failed = true;
      return;
    }
    // LEB128: seven bits per byte, high bit set while more follow.
    uint64_t rest = value;
    while (rest >= 0x80) {
      m_data.push_back(static_cast<uint8_t>(rest | 0x80));
      rest >>= 7;
    }
    m_data.push_back(static_cast<uint8_t>(rest));
  }
  void bytes(void *data, size_t count) {
    if (count > 0) {
      const uint8_t *p = static_cast<const uint8_t *>(data);
      m_data.insert(m_data.end(), p, p + count);
    }
  }
  void fail() {
    m_failed = true;
  }
  bool canRead(uint64_t) const {
    return true;
  }

private:
  std::vector<uint8_t> m_data;
  bool m_failed = false;
};

// Reads from a buffer it does not own; the buffer must outlive the reader.
class ByteReader {
public:
  static constexpr bool kReading = true;

  ByteReader(const uint8_t *data, size_t size) : m_data(data), m_size(size) {
  }
  explicit ByteReader(const std::vector<uint8_t> &bytes)
      : m_data(bytes.data()), m_size(bytes.size()) {
  }

  template <typename... Ts> ByteReader &operator()(Ts &...values) {
    (detail::archiveValue(*this, values), ...);
    return *this;
  }

  size_t remaining() const {
    return m_size - m_pos;
  }
  // Sticky: once set, everything reads as zero.
  bool failed() const {
    return m_failed;
  }

  void fixed(uint64_t &value, size_t size) {
    value = 0;
    if (m_failed || size > remaining()) {
      m_failed = true;
      return;
    }
    for (size_t i = 0; i < size; ++i) {
      value |= static_cast<uint64_t>(m_data[m_pos + i]) << (8 * i);
    }
    m_pos += size;
  }
  void length(uint64_t &value) {
    value = 0;
    // A uint32 needs at most 5 bytes, the last using only 4 bits.
    for (int i = 0; i < 5; ++i) {
      uint64_t byte = 0;
      fixed(byte, 1);
      if (m_failed) {
        value = 0;
        return;
      }
      value |= (byte & 0x7F) << (7 * i);
      if ((byte & 0x80) == 0) {
        if (value > std::numeric_limits<uint32_t>::max()) {
          m_failed = true;
          value = 0;
        }
        return;
      }
    }
    m_failed = true;
    value = 0;
  }
  void bytes(void *data, size_t count) {
    if (m_failed || count > remaining()) {
      m_failed = true;
      return;
    }
    if (count > 0) {
      std::memcpy(data, m_data + m_pos, count);
    }
    m_pos += count;
  }
  void fail() {
    m_failed = true;
  }
  // Sets failed() if `count` more bytes cannot exist.
  bool canRead(uint64_t count) {
    if (m_failed || count > remaining()) {
      m_failed = true;
      return false;
    }
    return true;
  }

private:
  const uint8_t *m_data;
  size_t m_size;
  size_t m_pos = 0;
  bool m_failed = false;
};

template <typename T> std::vector<uint8_t> toBytes(const T &value) {
  ByteWriter writer;
  writer(value);
  return writer.release();
}

// False if malformed or bytes are left over.
template <typename T>
bool fromBytes(const uint8_t *data, size_t size, T &value) {
  ByteReader reader(data, size);
  reader(value);
  return !reader.failed() && reader.remaining() == 0;
}

template <typename T>
bool fromBytes(const std::vector<uint8_t> &bytes, T &value) {
  return fromBytes(bytes.data(), bytes.size(), value);
}

} // namespace componeng::net
