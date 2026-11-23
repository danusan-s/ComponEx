#pragma once

#include "componeng/net/address.hpp"

#include <cstddef>
#include <string>

namespace componeng::net {

// Largest payload sendTo() accepts; under the path MTU so the network never
// fragments.
constexpr size_t kMaxPacketSize = 1200;

enum class IoStatus {
  Ok,
  // Nothing to read, or no room to send, right now; retry later.
  WouldBlock,
  Error,
};

// A non-blocking datagram endpoint; the transport is written against this.
// Delivery is best effort: Ok from sendTo() only means it was handed on.
class IDatagramSocket {
public:
  virtual ~IDatagramSocket() = default;

  // Sending to an address nobody listens on is not an error; a different
  // address family than the socket's is.
  virtual IoStatus sendTo(const Address &to, const void *data, size_t size) = 0;

  // A datagram larger than `capacity` is discarded, not truncated; use a
  // kMaxPacketSize buffer to never lose a valid one.
  virtual IoStatus recvFrom(void *buffer, size_t capacity, size_t &received,
                            Address &from) = 0;

  // 0 only checks, negative waits forever.
  virtual bool waitReadable(int timeoutMs) = 0;

  // The bound address, with the real port when opened on 0.
  virtual Address localAddress() const = 0;
  virtual bool isOpen() const = 0;
  virtual void close() = 0;

  virtual const std::string &lastError() const = 0;
};

} // namespace componeng::net
