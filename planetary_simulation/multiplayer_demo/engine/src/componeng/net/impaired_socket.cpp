#include "componeng/net/impaired_socket.hpp"

#include <utility>

namespace componeng::net {

ImpairedSocket::ImpairedSocket(IDatagramSocket &inner,
                               const Impairment &impairment)
    : m_inner(inner), m_impairment(impairment), m_rng(impairment.seed) {
}

void ImpairedSocket::setImpairment(const Impairment &impairment) {
  m_impairment = impairment;
}

double ImpairedSocket::chance() {
  return std::uniform_real_distribution<double>(0.0, 1.0)(m_rng);
}

IoStatus ImpairedSocket::sendTo(const Address &to, const void *data,
                                size_t size) {
  // Misuse is reported as a real socket would, not hidden as loss.
  if (size > kMaxPacketSize) {
    return m_inner.sendTo(to, data, size);
  }
  // A lost datagram still reports Ok: the sender cannot tell.
  if (chance() < m_impairment.loss) {
    return IoStatus::Ok;
  }
  schedule(to, data, size);
  if (chance() < m_impairment.duplicate) {
    schedule(to, data, size);
  }
  return IoStatus::Ok;
}

void ImpairedSocket::schedule(const Address &to, const void *data,
                              size_t size) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  double delay = m_impairment.latency + chance() * m_impairment.jitter;
  if (delay <= 0.0) {
    m_inner.sendTo(to, data, size);
    return;
  }
  m_pending.emplace(m_now + delay,
                    Pending{to, std::vector<uint8_t>(bytes, bytes + size)});
}

void ImpairedSocket::update(double now) {
  m_now = now;
  while (!m_pending.empty() && m_pending.begin()->first <= now) {
    Pending pending = std::move(m_pending.begin()->second);
    m_pending.erase(m_pending.begin());
    m_inner.sendTo(pending.to, pending.data.data(), pending.data.size());
  }
}

} // namespace componeng::net
