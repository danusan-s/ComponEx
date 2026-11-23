#pragma once

#include "componeng/net/idatagram_socket.hpp"
#include "componeng/net/nat/nat_discovery.hpp"

#include <memory>
#include <string>

namespace componeng::net {

// Lets the game socket also ask STUN servers where it appears to be. It must be
// the same socket: a NAT assigns a public port per socket, so only the game
// socket's own answer helps a peer. Everything passes through, except answers
// to pending STUN requests, which are withheld from the reader (the Transport).
class StunSocket : public IDatagramSocket {
public:
  explicit StunSocket(std::unique_ptr<IDatagramSocket> inner,
                      uint32_t seed = 0);

  NatDiscovery &discovery() {
    return m_discovery;
  }
  const NatDiscovery &discovery() const {
    return m_discovery;
  }
  // Sends due requests; call once a frame.
  void update(double now);

  IoStatus sendTo(const Address &to, const void *data, size_t size) override {
    return m_inner->sendTo(to, data, size);
  }
  IoStatus recvFrom(void *buffer, size_t capacity, size_t &received,
                    Address &from) override;
  bool waitReadable(int timeoutMs) override {
    return m_inner->waitReadable(timeoutMs);
  }
  Address localAddress() const override {
    return m_inner->localAddress();
  }
  bool isOpen() const override {
    return m_inner->isOpen();
  }
  void close() override {
    m_inner->close();
  }
  const std::string &lastError() const override {
    return m_inner->lastError();
  }

private:
  std::unique_ptr<IDatagramSocket> m_inner;
  NatDiscovery m_discovery;
};

} // namespace componeng::net
