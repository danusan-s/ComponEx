#include "componeng/net/nat/stun_socket.hpp"

namespace componeng::net {

StunSocket::StunSocket(std::unique_ptr<IDatagramSocket> inner, uint32_t seed)
    : m_inner(std::move(inner)), m_discovery(seed) {
}

void StunSocket::update(double now) {
  m_discovery.update(
      now, [this](const Address &to, const uint8_t *data, size_t size) {
        // A lost request is simply sent again later.
        m_inner->sendTo(to, data, size);
      });
}

IoStatus StunSocket::recvFrom(void *buffer, size_t capacity, size_t &received,
                              Address &from) {
  while (true) {
    IoStatus status = m_inner->recvFrom(buffer, capacity, received, from);
    if (status != IoStatus::Ok) {
      return status;
    }
    if (!m_discovery.handleDatagram(from, static_cast<const uint8_t *>(buffer),
                                    received)) {
      return IoStatus::Ok;
    }
  }
}

} // namespace componeng::net
