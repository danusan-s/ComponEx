#include "componeng/net/nat/stun.hpp"

#include <algorithm>
#include <cstring>

namespace componeng::net::stun {

namespace {

constexpr uint16_t kBindingRequest = 0x0001;
constexpr uint16_t kBindingSuccess = 0x0101;
constexpr uint16_t kAttrMappedAddress = 0x0001;
constexpr uint16_t kAttrXorMappedAddress = 0x0020;
constexpr uint8_t kFamilyIPv4 = 0x01;
constexpr uint8_t kFamilyIPv6 = 0x02;

uint16_t readU16(const uint8_t *p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

void writeU16(std::vector<uint8_t> &out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value));
}

// What an IPv6 address is XORed with: the cookie, then the transaction id (IPv4
// uses the first four bytes).
std::array<uint8_t, 16> xorMask(const TransactionId &id) {
  std::array<uint8_t, 16> mask{};
  mask[0] = static_cast<uint8_t>(kMagicCookie >> 24);
  mask[1] = static_cast<uint8_t>(kMagicCookie >> 16);
  mask[2] = static_cast<uint8_t>(kMagicCookie >> 8);
  mask[3] = static_cast<uint8_t>(kMagicCookie);
  std::copy(id.begin(), id.end(), mask.begin() + 4);
  return mask;
}

void writeHeader(std::vector<uint8_t> &out, uint16_t type, uint16_t length,
                 const TransactionId &id) {
  writeU16(out, type);
  writeU16(out, length);
  out.push_back(static_cast<uint8_t>(kMagicCookie >> 24));
  out.push_back(static_cast<uint8_t>(kMagicCookie >> 16));
  out.push_back(static_cast<uint8_t>(kMagicCookie >> 8));
  out.push_back(static_cast<uint8_t>(kMagicCookie));
  out.insert(out.end(), id.begin(), id.end());
}

// Reads the value of a (XOR-)MAPPED-ADDRESS attribute.
bool parseAddress(const uint8_t *value, size_t length, bool isXor,
                  const TransactionId &id, Address &out) {
  if (length < 4 || value[0] != 0) {
    return false;
  }
  uint8_t family = value[1];
  uint16_t port = readU16(value + 2);
  if (isXor) {
    port ^= static_cast<uint16_t>(kMagicCookie >> 16);
  }
  if (port == 0) {
    return false;
  }
  size_t size = family == kFamilyIPv4 ? 4 : family == kFamilyIPv6 ? 16 : 0;
  if (size == 0 || length != 4 + size) {
    return false;
  }
  uint8_t bytes[16];
  std::array<uint8_t, 16> mask = xorMask(id);
  for (size_t i = 0; i < size; ++i) {
    bytes[i] =
        isXor ? static_cast<uint8_t>(value[4 + i] ^ mask[i]) : value[4 + i];
  }
  out = Address::fromBytes(family == kFamilyIPv4 ? Address::Family::IPv4
                                                 : Address::Family::IPv6,
                           bytes, port);
  return true;
}

} // namespace

std::vector<uint8_t> makeBindingRequest(const TransactionId &id) {
  std::vector<uint8_t> out;
  out.reserve(kHeaderSize);
  writeHeader(out, kBindingRequest, 0, id);
  return out;
}

std::vector<uint8_t> makeBindingResponse(const TransactionId &id,
                                         const Address &mapped) {
  const bool v6 = mapped.family() == Address::Family::IPv6;
  const size_t addressSize = v6 ? 16 : 4;
  std::vector<uint8_t> out;
  writeHeader(out, kBindingSuccess, static_cast<uint16_t>(4 + 4 + addressSize),
              id);
  writeU16(out, kAttrXorMappedAddress);
  writeU16(out, static_cast<uint16_t>(4 + addressSize));
  out.push_back(0);
  out.push_back(v6 ? kFamilyIPv6 : kFamilyIPv4);
  writeU16(out, static_cast<uint16_t>(mapped.port() ^ (kMagicCookie >> 16)));
  std::array<uint8_t, 16> mask = xorMask(id);
  for (size_t i = 0; i < addressSize; ++i) {
    out.push_back(static_cast<uint8_t>(mapped.bytes()[i] ^ mask[i]));
  }
  return out;
}

bool looksLikeStun(const uint8_t *data, size_t size) {
  if (size < kHeaderSize || (data[0] & 0xC0) != 0) {
    return false;
  }
  return data[4] == static_cast<uint8_t>(kMagicCookie >> 24) &&
         data[5] == static_cast<uint8_t>(kMagicCookie >> 16) &&
         data[6] == static_cast<uint8_t>(kMagicCookie >> 8) &&
         data[7] == static_cast<uint8_t>(kMagicCookie);
}

bool parseBindingResponse(const uint8_t *data, size_t size, TransactionId &id,
                          Address &mapped) {
  if (!looksLikeStun(data, size) || readU16(data) != kBindingSuccess) {
    return false;
  }
  size_t length = readU16(data + 2);
  // Over UDP the message is the whole datagram; attributes are padded to four
  // bytes.
  if (length % 4 != 0 || kHeaderSize + length != size) {
    return false;
  }
  std::copy(data + 8, data + 20, id.begin());

  Address xorMapped;
  Address plainMapped;
  size_t pos = kHeaderSize;
  while (pos < size) {
    if (pos + 4 > size) {
      return false;
    }
    uint16_t type = readU16(data + pos);
    size_t attrLength = readU16(data + pos + 2);
    pos += 4;
    if (pos + attrLength > size) {
      return false;
    }
    if (type == kAttrXorMappedAddress) {
      if (!parseAddress(data + pos, attrLength, true, id, xorMapped)) {
        return false;
      }
    } else if (type == kAttrMappedAddress) {
      // Legacy attribute that may sit beside the XOR one; a bad one only
      // matters if it is all there is.
      parseAddress(data + pos, attrLength, false, id, plainMapped);
    }
    pos += (attrLength + 3) & ~static_cast<size_t>(3);
  }
  if (pos != size) {
    return false;
  }
  if (xorMapped.isValid()) {
    mapped = xorMapped;
    return true;
  }
  if (plainMapped.isValid()) {
    mapped = plainMapped;
    return true;
  }
  return false;
}

} // namespace componeng::net::stun
