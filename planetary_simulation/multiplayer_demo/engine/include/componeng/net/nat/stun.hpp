#pragma once

#include "componeng/net/address.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The part of STUN (RFC 5389) that learns what address the internet sees a
// socket at: a Binding request and reading the answer. The server reports the
// source address of the request, which behind a NAT is the one a remote peer
// must send to.
namespace componeng::net::stun {

constexpr size_t kHeaderSize = 20;
constexpr uint32_t kMagicCookie = 0x2112A442;

// Matches a request to its answer; random per request.
using TransactionId = std::array<uint8_t, 12>;

std::vector<uint8_t> makeBindingRequest(const TransactionId &id);

// For tests, or running a STUN server of one's own.
std::vector<uint8_t> makeBindingResponse(const TransactionId &id,
                                         const Address &mapped);

// Cheap check to tell STUN apart from other traffic on the same socket.
bool looksLikeStun(const uint8_t *data, size_t size);

// Reads a successful Binding response; false for anything else. Uses
// XOR-MAPPED-ADDRESS, or legacy MAPPED-ADDRESS if that is all there is. Unknown
// attributes are skipped.
bool parseBindingResponse(const uint8_t *data, size_t size, TransactionId &id,
                          Address &mapped);

} // namespace componeng::net::stun
