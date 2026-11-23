#pragma once

// Private: the wire format of the transport's packets. Integers are little
// endian.

#include "componeng/net/idatagram_socket.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace componeng::net {

// Never writes past kMaxPacketSize; an overflowing write is dropped and flagged
// (a bug detector: callers check remaining() first).
class PacketWriter {
public:
  size_t size() const {
    return m_size;
  }
  size_t remaining() const {
    return kMaxPacketSize - m_size;
  }
  bool overflowed() const {
    return m_overflowed;
  }
  const uint8_t *data() const {
    return m_data;
  }

  void u8(uint8_t value) {
    bytes(&value, 1);
  }
  void u16(uint16_t value) {
    const uint8_t b[2] = {static_cast<uint8_t>(value),
                          static_cast<uint8_t>(value >> 8)};
    bytes(b, 2);
  }
  void u32(uint32_t value) {
    const uint8_t b[4] = {
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    bytes(b, 4);
  }
  void bytes(const void *data, size_t count) {
    if (count > remaining()) {
      m_overflowed = true;
      return;
    }
    if (count > 0) {
      std::memcpy(m_data + m_size, data, count);
    }
    m_size += count;
  }

private:
  uint8_t m_data[kMaxPacketSize];
  size_t m_size = 0;
  bool m_overflowed = false;
};

// Reading past the end returns zeros and sets failed(), so a parser can check
// once.
class PacketReader {
public:
  PacketReader(const uint8_t *data, size_t size) : m_data(data), m_size(size) {
  }

  size_t remaining() const {
    return m_size - m_pos;
  }
  bool failed() const {
    return m_failed;
  }

  uint8_t u8() {
    const uint8_t *p = take(1);
    return p ? p[0] : 0;
  }
  uint16_t u16() {
    const uint8_t *p = take(2);
    return p ? static_cast<uint16_t>(p[0] | (p[1] << 8)) : 0;
  }
  uint32_t u32() {
    const uint8_t *p = take(4);
    return p ? static_cast<uint32_t>(p[0]) |
                   (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) |
                   (static_cast<uint32_t>(p[3]) << 24)
             : 0;
  }
  const uint8_t *bytes(size_t count) {
    return take(count);
  }

private:
  const uint8_t *take(size_t count) {
    if (m_failed || count > remaining()) {
      m_failed = true;
      return nullptr;
    }
    const uint8_t *p = m_data + m_pos;
    m_pos += count;
    return p;
  }

  const uint8_t *m_data;
  size_t m_size;
  size_t m_pos = 0;
  bool m_failed = false;
};

// Wire layout shared by Transport and Connection.
//
// Every packet starts with: protocol id u32, type u8.
//   Connect:    salt u32, zero padding up to kConnectPacketSize
//   Accept:     echoed salt u32, sender's own salt u32
//   Reject:     echoed salt u32
//   Disconnect: connection id u32
//   Punch:      nothing; only there to be sent (see Transport::punch)
//   Data:       connection id u32, sequence u16, ack u16, ack bits u32, entries
// A Data entry is: flags u8 (bit 0 reliable, bit 1 more fragments follow),
// message id u16 (reliable only), length u16, payload.
enum class PacketType : uint8_t {
  Connect = 1,
  Accept = 2,
  Reject = 3,
  Data = 4,
  Disconnect = 5,
  Punch = 6
};

constexpr size_t kCommonHeaderSize = 5;
constexpr size_t kDataHeaderSize = kCommonHeaderSize + 4 + 2 + 2 + 4;
constexpr size_t kUnreliableEntryOverhead = 1 + 2;
constexpr size_t kReliableEntryOverhead = 1 + 2 + 2;
// Reliable messages are split into packet-sized fragments.
constexpr size_t kMaxReliableFragment =
    kMaxPacketSize - kDataHeaderSize - kReliableEntryOverhead;
// Requests are padded to this size so a forged one cannot make us send a
// bigger reply to a victim.
constexpr size_t kConnectPacketSize = 64;

constexpr uint8_t kEntryReliable = 1;
constexpr uint8_t kEntryMore = 2;

} // namespace componeng::net
