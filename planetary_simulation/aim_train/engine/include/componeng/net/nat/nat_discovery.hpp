#pragma once

#include "componeng/net/address.hpp"
#include "componeng/net/nat/stun.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace componeng::net {

// How a NAT assigns public addresses, which decides whether peers can reach
// each other directly.
enum class NatType : uint8_t {
  // Not worked out (one server answered, or discovery has not finished).
  Unknown,
  // Same public address for every destination: hole punching works.
  Cone,
  // A different public port per destination: the STUN-reported address is only
  // valid toward the STUN server, so direct connection fails (unless the other
  // side is open). Needs a relay, which the engine does not provide.
  Symmetric
};

struct NatInfo {
  // The address the internet sees this socket at; invalid until learned.
  Address publicAddress;
  NatType type = NatType::Unknown;
};

// Learns a socket's public address from STUN servers and, when two answer,
// whether the NAT keeps it across destinations. Does no I/O: the owner feeds it
// received datagrams and sends what it asks for (see StunSocket).
class NatDiscovery {
public:
  enum class State : uint8_t {
    Idle,     // nothing asked yet
    Querying, // waiting for answers
    Done,     // at least one server answered
    Failed    // none did
  };

  using SendFunction =
      std::function<void(const Address &to, const uint8_t *data, size_t size)>;

  explicit NatDiscovery(uint32_t seed = 0);

  // Up to three servers are used; the NAT type needs two that answer. An empty
  // list fails at once.
  void start(const std::vector<Address> &servers);
  // Sends due requests; finishes once all are answered or given up. Call every
  // frame while Querying.
  void update(double now, const SendFunction &send);
  // True if the datagram answered one of our requests (the caller must then not
  // treat it as anything else). Not taken: STUN-shaped but not ours, or from
  // the wrong source.
  bool handleDatagram(const Address &from, const uint8_t *data, size_t size);

  State state() const {
    return m_state;
  }
  // The address is known from the first answer, before the state reaches Done.
  const NatInfo &info() const {
    return m_info;
  }

  // A request is sent up to kMaxAttempts times, backing off, before its server
  // is given up on.
  static constexpr double kRetryBase = 0.5;
  static constexpr int kMaxAttempts = 4;
  static constexpr size_t kMaxServers = 3;

private:
  struct Query {
    Address server;
    stun::TransactionId id{};
    int attempts = 0;
    double nextSend = 0.0;
    bool answered = false;
    bool gaveUp = false;
    Address mapped;
  };

  void finishIfSettled();

  State m_state = State::Idle;
  NatInfo m_info;
  std::vector<Query> m_queries;
  std::mt19937 m_rng;
};

} // namespace componeng::net
