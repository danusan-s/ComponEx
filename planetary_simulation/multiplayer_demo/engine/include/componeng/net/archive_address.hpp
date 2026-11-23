#pragma once

// Address as a message field: family byte, port, 16 address bytes (19 bytes
// always).

#include "componeng/net/address.hpp"
#include "componeng/net/archive.hpp"

#include <algorithm>
#include <array>

namespace componeng::net {

template <typename Ar> void serialize(Ar &ar, Address &address) {
  uint8_t family = static_cast<uint8_t>(address.family());
  uint16_t port = address.port();
  std::array<uint8_t, Address::kMaxBytes> bytes{};
  if constexpr (!Ar::kReading) {
    std::copy(address.bytes(), address.bytes() + Address::kMaxBytes,
              bytes.begin());
  }
  ar(family, port, bytes);
  if constexpr (Ar::kReading) {
    if (family > static_cast<uint8_t>(Address::Family::IPv6)) {
      ar.fail();
      return;
    }
    address = Address::fromBytes(static_cast<Address::Family>(family),
                                 bytes.data(), port);
  }
}

} // namespace componeng::net
