#include "componeng/net/address.hpp"

#include "platform.hpp"

#include <cstring>

namespace componeng::net {

namespace {

// Digits only, 0-65535; no sign, space or junk, and no exceptions.
bool parsePort(const std::string &text, uint16_t &port) {
  if (text.empty() || text.size() > 5) {
    return false;
  }
  uint32_t value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + static_cast<uint32_t>(c - '0');
  }
  if (value > 65535) {
    return false;
  }
  port = static_cast<uint16_t>(value);
  return true;
}

} // namespace

Address Address::ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                      uint16_t port) {
  const uint8_t bytes[4] = {a, b, c, d};
  return fromBytes(Family::IPv4, bytes, port);
}

Address Address::fromBytes(Family family, const uint8_t *bytes, uint16_t port) {
  Address address;
  if (family == Family::None) {
    return address;
  }
  address.m_family = family;
  address.m_port = port;
  std::memcpy(address.m_bytes, bytes, family == Family::IPv4 ? 4 : 16);
  return address;
}

Address Address::any(uint16_t port, Family family) {
  const uint8_t zeros[kMaxBytes] = {};
  return fromBytes(family, zeros, port);
}

Address Address::loopback(uint16_t port, Family family) {
  if (family == Family::IPv6) {
    uint8_t bytes[kMaxBytes] = {};
    bytes[15] = 1;
    return fromBytes(family, bytes, port);
  }
  return ipv4(127, 0, 0, 1, port);
}

bool Address::parse(const std::string &text, Address &out) {
  std::string host;
  std::string portText;
  Family family = Family::IPv4;

  if (!text.empty() && text.front() == '[') {
    size_t close = text.find(']');
    if (close == std::string::npos || close + 1 >= text.size() ||
        text[close + 1] != ':') {
      return false;
    }
    family = Family::IPv6;
    host = text.substr(1, close - 1);
    portText = text.substr(close + 2);
  } else {
    // More than one ':' is a bare IPv6 address, which needs brackets to carry a
    // port.
    size_t colon = text.find(':');
    if (colon == std::string::npos || colon != text.rfind(':')) {
      return false;
    }
    host = text.substr(0, colon);
    portText = text.substr(colon + 1);
  }

  uint16_t port = 0;
  if (!parsePort(portText, port)) {
    return false;
  }
  uint8_t bytes[kMaxBytes] = {};
  int result = inet_pton(family == Family::IPv6 ? AF_INET6 : AF_INET,
                         host.c_str(), bytes);
  if (result != 1) {
    return false;
  }
  out = fromBytes(family, bytes, port);
  return true;
}

bool Address::resolve(const std::string &host, uint16_t port, Family family,
                      Address &out) {
  if (host.empty() || family == Family::None || !platform::acquireNetwork()) {
    return false;
  }
  const int af = family == Family::IPv6 ? AF_INET6 : AF_INET;
  addrinfo hints{};
  hints.ai_family = af;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo *results = nullptr;
  bool found = false;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &results) == 0) {
    for (const addrinfo *r = results; r && !found; r = r->ai_next) {
      if (r->ai_family != af) {
        continue;
      }
      if (af == AF_INET) {
        const auto *in = reinterpret_cast<const sockaddr_in *>(r->ai_addr);
        out = fromBytes(family,
                        reinterpret_cast<const uint8_t *>(&in->sin_addr), port);
      } else {
        const auto *in6 = reinterpret_cast<const sockaddr_in6 *>(r->ai_addr);
        out = fromBytes(
            family, reinterpret_cast<const uint8_t *>(&in6->sin6_addr), port);
      }
      found = true;
    }
    freeaddrinfo(results);
  }
  platform::releaseNetwork();
  return found;
}

std::string Address::toString() const {
  if (m_family == Family::None) {
    return "(invalid)";
  }
  char buffer[INET6_ADDRSTRLEN] = {};
  bool v6 = m_family == Family::IPv6;
  if (!inet_ntop(v6 ? AF_INET6 : AF_INET, m_bytes, buffer, sizeof(buffer))) {
    return "(invalid)";
  }
  std::string host = buffer;
  if (v6) {
    host = "[" + host + "]";
  }
  return host + ":" + std::to_string(m_port);
}

size_t Address::hash() const {
  // FNV-1a over everything == compares.
  uint64_t h = 14695981039346656037ull;
  auto mix = [&h](uint8_t byte) {
    h ^= byte;
    h *= 1099511628211ull;
  };
  mix(static_cast<uint8_t>(m_family));
  mix(static_cast<uint8_t>(m_port & 0xff));
  mix(static_cast<uint8_t>(m_port >> 8));
  for (uint8_t byte : m_bytes) {
    mix(byte);
  }
  return static_cast<size_t>(h);
}

bool Address::operator==(const Address &other) const {
  // Unused bytes are zero, so comparing all of them is exact.
  return m_family == other.m_family && m_port == other.m_port &&
         std::memcmp(m_bytes, other.m_bytes, kMaxBytes) == 0;
}

} // namespace componeng::net
