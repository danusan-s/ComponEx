#include "componeng/net/nat/nat_discovery.hpp"

#include <algorithm>

namespace componeng::net {

NatDiscovery::NatDiscovery(uint32_t seed)
    : m_rng(seed != 0 ? seed : std::random_device{}()) {
}

void NatDiscovery::start(const std::vector<Address> &servers) {
  m_queries.clear();
  m_info = NatInfo();
  for (const Address &server : servers) {
    bool duplicate =
        std::any_of(m_queries.begin(), m_queries.end(),
                    [&](const Query &q) { return q.server == server; });
    if (!server.isValid() || duplicate || m_queries.size() >= kMaxServers) {
      continue;
    }
    Query query;
    query.server = server;
    for (uint8_t &byte : query.id) {
      byte = static_cast<uint8_t>(m_rng());
    }
    m_queries.push_back(query);
  }
  m_state = m_queries.empty() ? State::Failed : State::Querying;
}

void NatDiscovery::update(double now, const SendFunction &send) {
  if (m_state != State::Querying) {
    return;
  }
  for (Query &query : m_queries) {
    if (query.answered || query.gaveUp || now < query.nextSend) {
      continue;
    }
    if (query.attempts >= kMaxAttempts) {
      query.gaveUp = true;
      continue;
    }
    std::vector<uint8_t> request = stun::makeBindingRequest(query.id);
    send(query.server, request.data(), request.size());
    // Back off 0.5 s, 1 s, then 2 s each; give up 5.5 s after the first send.
    query.nextSend = now + kRetryBase * static_cast<double>(
                                            1 << std::min(query.attempts, 2));
    ++query.attempts;
  }
  finishIfSettled();
}

bool NatDiscovery::handleDatagram(const Address &from, const uint8_t *data,
                                  size_t size) {
  if (m_state != State::Querying || !stun::looksLikeStun(data, size)) {
    return false;
  }
  stun::TransactionId id;
  Address mapped;
  if (!stun::parseBindingResponse(data, size, id, mapped)) {
    return false;
  }
  for (Query &query : m_queries) {
    // The random transaction id nearly proves a match; the source check stops a
    // third party claiming an answer.
    if (query.id != id || query.server != from) {
      continue;
    }
    if (!query.answered) {
      query.answered = true;
      query.mapped = mapped;
      if (!m_info.publicAddress.isValid()) {
        m_info.publicAddress = mapped;
      }
      finishIfSettled();
    }
    return true;
  }
  return false;
}

void NatDiscovery::finishIfSettled() {
  for (const Query &query : m_queries) {
    if (!query.answered && !query.gaveUp) {
      return;
    }
  }
  std::vector<const Query *> answered;
  for (const Query &query : m_queries) {
    if (query.answered) {
      answered.push_back(&query);
    }
  }
  if (answered.empty()) {
    m_state = State::Failed;
    return;
  }

  m_info.publicAddress = answered[0]->mapped;
  // Compare what two different servers saw: one public address for everything
  // is a cone, a different one per destination is symmetric.
  for (size_t i = 1; i < answered.size(); ++i) {
    if (answered[0]->mapped != answered[i]->mapped) {
      m_info.type = NatType::Symmetric;
      break;
    }
    m_info.type = NatType::Cone;
  }
  m_state = State::Done;
}

} // namespace componeng::net
