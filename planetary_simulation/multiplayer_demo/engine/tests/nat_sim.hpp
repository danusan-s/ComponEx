#pragma once

// A stand-in for the internet and the routers on it, for testing NAT
// traversal: a LoopbackNetwork is "the internet", a NatSocket is a peer's
// socket as seen through its router, and MockStunServer answers Binding
// requests on it.

#include "componeng/net/idatagram_socket.hpp"
#include "componeng/net/loopback_socket.hpp"
#include "componeng/net/nat/stun.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace nat_sim {

using namespace componeng::net;

enum class NatKind {
  // Anyone may send to a public address once the peer has used it.
  FullCone,
  // Only the addresses the peer has itself sent to may send back (what most
  // home routers do).
  PortRestricted,
  // A new public address for every destination, and only that destination may
  // send back.
  Symmetric
};

struct AddressLess {
  bool operator()(const Address &a, const Address &b) const {
    return a.toString() < b.toString();
  }
};

// The socket of a peer behind a NAT: what it sends leaves from a public address
// the NAT assigns, and what arrives is let through or dropped by the NAT's
// rules. localAddress() is the private address, which no one else can use.
class NatSocket : public IDatagramSocket {
public:
  NatSocket(LoopbackNetwork &internet, NatKind kind, uint16_t privatePort)
      : m_internet(internet), m_kind(kind),
        m_private(Address::ipv4(192, 168, 1, 10, privatePort)) {
  }

  IoStatus sendTo(const Address &to, const void *data, size_t size) override {
    Mapping &mapping = mappingFor(to);
    if (m_kind != NatKind::FullCone) {
      mapping.allowed.insert(to);
    }
    return mapping.socket->sendTo(to, data, size);
  }

  IoStatus recvFrom(void *buffer, size_t capacity, size_t &received,
                    Address &from) override {
    for (auto &entry : m_mappings) {
      Mapping &mapping = entry.second;
      while (mapping.socket->recvFrom(buffer, capacity, received, from) ==
             IoStatus::Ok) {
        if (m_kind == NatKind::FullCone || mapping.allowed.count(from)) {
          return IoStatus::Ok;
        }
        ++dropped;
      }
    }
    return IoStatus::WouldBlock;
  }

  bool waitReadable(int) override {
    for (auto &entry : m_mappings) {
      if (entry.second.socket->waitReadable(0)) {
        return true;
      }
    }
    return false;
  }
  Address localAddress() const override {
    return m_private;
  }
  bool isOpen() const override {
    return true;
  }
  void close() override {
    m_mappings.clear();
  }
  const std::string &lastError() const override {
    return m_error;
  }

  Address publicAddressToward(const Address &peer) {
    return Address::loopback(mappingFor(peer).socket->localAddress().port());
  }

  int dropped = 0;

private:
  struct Mapping {
    std::unique_ptr<LoopbackSocket> socket;
    std::set<Address, AddressLess> allowed;
  };

  // One mapping for everything, except a symmetric NAT, which has one per
  // destination.
  Mapping &mappingFor(const Address &to) {
    Address key = m_kind == NatKind::Symmetric ? to : Address();
    auto it = m_mappings.find(key);
    if (it == m_mappings.end()) {
      Mapping mapping;
      mapping.socket = m_internet.open();
      it = m_mappings.emplace(key, std::move(mapping)).first;
    }
    return it->second;
  }

  LoopbackNetwork &m_internet;
  NatKind m_kind;
  Address m_private;
  std::map<Address, Mapping, AddressLess> m_mappings;
  std::string m_error;
};

// A public STUN server: answers each Binding request with the address it came
// from.
class MockStunServer {
public:
  MockStunServer(LoopbackNetwork &internet, uint16_t port)
      : m_socket(internet.open(port)) {
  }

  Address address() const {
    return m_socket->localAddress();
  }

  void update() {
    uint8_t buffer[kMaxPacketSize];
    size_t received = 0;
    Address from;
    while (m_socket->recvFrom(buffer, sizeof(buffer), received, from) ==
           IoStatus::Ok) {
      ++requests;
      if (requests <= ignoreFirst || silent ||
          !stun::looksLikeStun(buffer, received)) {
        continue;
      }
      stun::TransactionId id;
      std::copy(buffer + 8, buffer + 20, id.begin());
      std::vector<uint8_t> response = stun::makeBindingResponse(id, from);
      m_socket->sendTo(from, response.data(), response.size());
    }
  }

  int requests = 0;
  // Drops this many requests before answering, as a lossy network would.
  int ignoreFirst = 0;
  bool silent = false;

private:
  std::unique_ptr<LoopbackSocket> m_socket;
};

} // namespace nat_sim
