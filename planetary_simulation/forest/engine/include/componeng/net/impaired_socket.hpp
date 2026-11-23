#pragma once

#include "componeng/net/idatagram_socket.hpp"

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace componeng::net {

struct Impairment {
  // Chance (0-1) that an outgoing datagram is dropped.
  double loss = 0.0;
  // Chance that an outgoing datagram is sent twice.
  double duplicate = 0.0;
  // Delay in seconds plus a random 0..jitter; jitter reorders.
  double latency = 0.0;
  double jitter = 0.0;
  uint32_t seed = 1;
};

// Makes what a socket sends behave like a bad network (lost, duplicated,
// late, reordered); send direction only. Call update(now) with the transport's
// clock; the inner socket must outlive this.
class ImpairedSocket : public IDatagramSocket {
public:
  ImpairedSocket(IDatagramSocket &inner, const Impairment &impairment);

  void setImpairment(const Impairment &impairment);
  void update(double now);

  IoStatus sendTo(const Address &to, const void *data, size_t size) override;
  IoStatus recvFrom(void *buffer, size_t capacity, size_t &received,
                    Address &from) override {
    return m_inner.recvFrom(buffer, capacity, received, from);
  }
  bool waitReadable(int timeoutMs) override {
    return m_inner.waitReadable(timeoutMs);
  }
  Address localAddress() const override {
    return m_inner.localAddress();
  }
  bool isOpen() const override {
    return m_inner.isOpen();
  }
  void close() override {
    m_inner.close();
  }
  const std::string &lastError() const override {
    return m_inner.lastError();
  }

private:
  struct Pending {
    Address to;
    std::vector<uint8_t> data;
  };

  void schedule(const Address &to, const void *data, size_t size);
  double chance();

  IDatagramSocket &m_inner;
  Impairment m_impairment;
  std::mt19937 m_rng;
  double m_now = 0.0;
  std::multimap<double, Pending> m_pending;
};

} // namespace componeng::net
