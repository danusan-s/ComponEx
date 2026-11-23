#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace componeng::net {

// An IPv4/IPv6 address and port; copyable, comparable and hashable (the
// transport keys connections by it). Bytes are network order, port host order.
// A default-constructed Address is invalid.
class Address {
public:
  enum class Family : uint8_t { None, IPv4, IPv6 };

  static constexpr size_t kMaxBytes = 16;

  Address() = default;

  static Address ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                      uint16_t port);
  // 4 (IPv4) or 16 (IPv6) bytes in network order.
  static Address fromBytes(Family family, const uint8_t *bytes, uint16_t port);
  // The wildcard address; port 0 lets the OS pick one.
  static Address any(uint16_t port = 0, Family family = Family::IPv4);
  static Address loopback(uint16_t port, Family family = Family::IPv4);

  // "1.2.3.4:5000" or "[::1]:5000". Numeric only (no DNS); `out` is left
  // untouched on failure.
  static bool parse(const std::string &text, Address &out);

  // Blocking name lookup (can take seconds): keep it off the game loop.
  static bool resolve(const std::string &host, uint16_t port, Family family,
                      Address &out);

  bool isValid() const {
    return m_family != Family::None;
  }
  Family family() const {
    return m_family;
  }
  uint16_t port() const {
    return m_port;
  }
  // Unused bytes are zero.
  const uint8_t *bytes() const {
    return m_bytes;
  }

  std::string toString() const;
  size_t hash() const;

  bool operator==(const Address &other) const;
  bool operator!=(const Address &other) const {
    return !(*this == other);
  }

private:
  Family m_family = Family::None;
  uint16_t m_port = 0;
  uint8_t m_bytes[kMaxBytes] = {};
};

} // namespace componeng::net

namespace std {
template <> struct hash<componeng::net::Address> {
  size_t operator()(const componeng::net::Address &address) const noexcept {
    return address.hash();
  }
};
} // namespace std
