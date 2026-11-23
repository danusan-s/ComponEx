#include "componeng/net/loopback_socket.hpp"

#include <cstring>

namespace componeng::net {

LoopbackSocket::LoopbackSocket(LoopbackNetwork &network, uint16_t port)
    : m_network(&network), m_local(Address::loopback(port)) {
}

LoopbackSocket::~LoopbackSocket() {
  close();
}

void LoopbackSocket::close() {
  if (!m_network) {
    return;
  }
  m_network->release(m_local.port());
  m_network = nullptr;
  m_queue.clear();
}

IoStatus LoopbackSocket::sendTo(const Address &to, const void *data,
                                size_t size) {
  if (!isOpen()) {
    m_lastError = "socket is not open";
    return IoStatus::Error;
  }
  if (size > kMaxPacketSize) {
    m_lastError = "datagram of " + std::to_string(size) + " bytes exceeds " +
                  std::to_string(kMaxPacketSize);
    return IoStatus::Error;
  }
  if (to.family() != m_local.family()) {
    m_lastError =
        "destination is of a different address family than the socket";
    return IoStatus::Error;
  }
  m_network->deliver(m_local, to, data, size);
  return IoStatus::Ok;
}

IoStatus LoopbackSocket::recvFrom(void *buffer, size_t capacity,
                                  size_t &received, Address &from) {
  received = 0;
  if (!isOpen()) {
    m_lastError = "socket is not open";
    return IoStatus::Error;
  }
  while (!m_queue.empty()) {
    Datagram datagram = std::move(m_queue.front());
    m_queue.pop_front();
    // Oversized datagrams are discarded, as in UdpSocket.
    if (datagram.data.size() > capacity) {
      continue;
    }
    if (!datagram.data.empty()) {
      std::memcpy(buffer, datagram.data.data(), datagram.data.size());
    }
    received = datagram.data.size();
    from = datagram.from;
    return IoStatus::Ok;
  }
  return IoStatus::WouldBlock;
}

bool LoopbackSocket::waitReadable(int) {
  return !m_queue.empty();
}

void LoopbackSocket::enqueue(const Address &from, const void *data,
                             size_t size) {
  if (m_queue.size() >= kMaxQueued) {
    return;
  }
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  m_queue.push_back({from, std::vector<uint8_t>(bytes, bytes + size)});
}

LoopbackNetwork::~LoopbackNetwork() {
  // Orphan the sockets; their owners keep them.
  for (auto &entry : m_sockets) {
    entry.second->m_network = nullptr;
    entry.second->m_queue.clear();
  }
}

std::unique_ptr<LoopbackSocket> LoopbackNetwork::open(uint16_t port) {
  if (port == 0) {
    // Walk the ephemeral range once, starting after the last port handed out.
    const uint32_t count = 65536u - kFirstEphemeralPort;
    for (uint32_t i = 0; i < count; ++i) {
      uint16_t candidate = m_nextEphemeralPort;
      m_nextEphemeralPort = candidate == 65535
                                ? kFirstEphemeralPort
                                : static_cast<uint16_t>(candidate + 1);
      if (m_sockets.find(candidate) == m_sockets.end()) {
        port = candidate;
        break;
      }
    }
    if (port == 0) {
      return nullptr;
    }
  } else if (m_sockets.find(port) != m_sockets.end()) {
    return nullptr;
  }

  std::unique_ptr<LoopbackSocket> socket(new LoopbackSocket(*this, port));
  m_sockets.emplace(port, socket.get());
  return socket;
}

void LoopbackNetwork::deliver(const Address &from, const Address &to,
                              const void *data, size_t size) {
  // Only 127.0.0.1 exists. A broadcast reaches whoever holds the port (ports
  // are not shared on this network).
  const bool broadcast = to == Address::ipv4(255, 255, 255, 255, to.port());
  if (!broadcast && to != Address::loopback(to.port(), to.family())) {
    return;
  }
  auto it = m_sockets.find(to.port());
  if (it != m_sockets.end()) {
    it->second->enqueue(from, data, size);
  }
}

void LoopbackNetwork::release(uint16_t port) {
  m_sockets.erase(port);
}

} // namespace componeng::net
