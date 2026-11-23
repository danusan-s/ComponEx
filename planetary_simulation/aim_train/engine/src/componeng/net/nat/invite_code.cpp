#include "componeng/net/nat/invite_code.hpp"

#include <cstdint>
#include <vector>

namespace componeng::net {

namespace {

constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

// Bytes before encoding: version (4 or 6), address, port, CRC-8. CRC-8 catches
// every error within eight bits, so any single wrong character is detected.
uint8_t crc8(const std::vector<uint8_t> &bytes) {
  uint8_t crc = 0;
  for (uint8_t byte : bytes) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
      crc = static_cast<uint8_t>((crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1);
    }
  }
  return crc;
}

int decodeChar(char c) {
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }
  if (c == 'O') {
    c = '0';
  } else if (c == 'I' || c == 'L') {
    c = '1';
  }
  for (int i = 0; i < 32; ++i) {
    if (kAlphabet[i] == c) {
      return i;
    }
  }
  return -1;
}

} // namespace

std::string encodeInvite(const Address &address) {
  if (!address.isValid()) {
    return std::string();
  }
  const bool v6 = address.family() == Address::Family::IPv6;
  std::vector<uint8_t> bytes;
  bytes.push_back(v6 ? 6 : 4);
  bytes.insert(bytes.end(), address.bytes(), address.bytes() + (v6 ? 16 : 4));
  bytes.push_back(static_cast<uint8_t>(address.port() >> 8));
  bytes.push_back(static_cast<uint8_t>(address.port()));
  bytes.push_back(crc8(bytes));

  std::string text;
  uint32_t buffer = 0;
  int bits = 0;
  for (uint8_t byte : bytes) {
    buffer = (buffer << 8) | byte;
    bits += 8;
    while (bits >= 5) {
      text.push_back(kAlphabet[(buffer >> (bits - 5)) & 31]);
      bits -= 5;
    }
  }
  if (bits > 0) {
    text.push_back(kAlphabet[(buffer << (5 - bits)) & 31]);
  }

  std::string grouped;
  for (size_t i = 0; i < text.size(); ++i) {
    if (i > 0 && i % 4 == 0) {
      grouped.push_back('-');
    }
    grouped.push_back(text[i]);
  }
  return grouped;
}

bool decodeInvite(const std::string &code, Address &out) {
  std::vector<uint8_t> bytes;
  uint32_t buffer = 0;
  int bits = 0;
  size_t symbols = 0;
  for (char c : code) {
    if (c == '-' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      continue;
    }
    int value = decodeChar(c);
    if (value < 0) {
      return false;
    }
    ++symbols;
    buffer = (buffer << 5) | static_cast<uint32_t>(value);
    bits += 5;
    if (bits >= 8) {
      bytes.push_back(static_cast<uint8_t>(buffer >> (bits - 8)));
      bits -= 8;
    }
  }
  // Leftover bits are padding and must be zero, or another text would decode to
  // the same address.
  if (bits > 0 && (buffer & ((1u << bits) - 1)) != 0) {
    return false;
  }

  if (bytes.empty() || (bytes[0] != 4 && bytes[0] != 6)) {
    return false;
  }
  const bool v6 = bytes[0] == 6;
  const size_t addressSize = v6 ? 16 : 4;
  if (bytes.size() != 1 + addressSize + 2 + 1) {
    return false;
  }
  // Exactly as many symbols as the bytes need, or trailing zeros would be a
  // second spelling.
  if (symbols != (bytes.size() * 8 + 4) / 5) {
    return false;
  }
  uint8_t expected = bytes.back();
  bytes.pop_back();
  if (crc8(bytes) != expected) {
    return false;
  }
  uint16_t port = static_cast<uint16_t>((bytes[1 + addressSize] << 8) |
                                        bytes[2 + addressSize]);
  out = Address::fromBytes(v6 ? Address::Family::IPv6 : Address::Family::IPv4,
                           bytes.data() + 1, port);
  return true;
}

} // namespace componeng::net
