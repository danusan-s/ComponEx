#pragma once

#include "componeng/net/address.hpp"
#include "componeng/net/idatagram_socket.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace componeng::net {

class LoopbackNetwork;

// In-memory stand-in for a UdpSocket bound to 127.0.0.1; datagrams sent to a
// port nobody holds vanish.
class LoopbackSocket : public IDatagramSocket {
public:
  ~LoopbackSocket() override;

  // The network points at its sockets, so they cannot move.
  LoopbackSocket(const LoopbackSocket &) = delete;
  LoopbackSocket &operator=(const LoopbackSocket &) = delete;
  LoopbackSocket(LoopbackSocket &&) = delete;
  LoopbackSocket &operator=(LoopbackSocket &&) = delete;

  IoStatus sendTo(const Address &to, const void *data, size_t size) override;
  IoStatus recvFrom(void *buffer, size_t capacity, size_t &received,
                    Address &from) override;
  // Never sleeps (single-threaded): reports whether a datagram is queued.
  bool waitReadable(int timeoutMs) override;

  Address localAddress() const override {
    return m_local;
  }
  bool isOpen() const override {
    return m_network != nullptr;
  }
  void close() override;
  const std::string &lastError() const override {
    return m_lastError;
  }

  // Arrivals beyond this are dropped, like a full OS buffer.
  static constexpr size_t kMaxQueued = 1024;

private:
  friend class LoopbackNetwork;

  struct Datagram {
    Address from;
    std::vector<uint8_t> data;
  };

  LoopbackSocket(LoopbackNetwork &network, uint16_t port);
  void enqueue(const Address &from, const void *data, size_t size);

  // Null once closed or once the network is gone.
  LoopbackNetwork *m_network;
  Address m_local;
  std::deque<Datagram> m_queue;
  std::string m_lastError;
};

// Isolated in-process network, for tests and several peers in one process.
class LoopbackNetwork {
public:
  LoopbackNetwork() = default;
  // Open sockets stay usable but reach nobody.
  ~LoopbackNetwork();

  LoopbackNetwork(const LoopbackNetwork &) = delete;
  LoopbackNetwork &operator=(const LoopbackNetwork &) = delete;

  // Port 0 picks a free one; null if the port is taken.
  std::unique_ptr<LoopbackSocket> open(uint16_t port = 0);

private:
  friend class LoopbackSocket;

  void deliver(const Address &from, const Address &to, const void *data,
               size_t size);
  void release(uint16_t port);

  static constexpr uint16_t kFirstEphemeralPort = 49152;

  std::unordered_map<uint16_t, LoopbackSocket *> m_sockets;
  uint16_t m_nextEphemeralPort = kFirstEphemeralPort;
};

} // namespace componeng::net
