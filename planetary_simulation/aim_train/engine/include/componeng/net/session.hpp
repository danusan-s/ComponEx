#pragma once

#include "componeng/net/address.hpp"
#include "componeng/net/lan_discovery.hpp"
#include "componeng/net/network.hpp"
#include "componeng/net/session_types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace componeng::net {

struct SessionConfig {
  // Shown when browsing the LAN (cut to kMaxSessionNameLength); joiners take
  // the host's.
  std::string name = "Game";
  // Peers including this one (host only; joiners learn it). Keep it a few under
  // TransportConfig::maxConnections: every peer connects to every other.
  uint16_t maxPeers = 8;

  // Answer LAN searches while this peer is the leader.
  bool advertiseOnLan = true;
  uint16_t discoveryPort = kDefaultDiscoveryPort;
  // Makes the discovery sockets; empty uses real UDP. For tests.
  LanSocketFactory discoverySockets;

  // How long join() may take, handshakes with every peer included.
  double joinTimeout = 15.0;
};

// Peers that all talk to each other with no server: one hosts, others join,
// and every peer ends up connected to every other (a mesh), named by PeerId.
// The leader admits joiners and answers LAN searches: the host, or if it
// leaves, the lowest id; everything else is symmetric.
//
// One Session per Network; update(now) each frame (NetPlugin's system does it)
// and callbacks run inside it. Call leave() before the Network goes away so
// peers are told. A broken link between two peers that both stay connected to
// the rest is not repaired.
class Session {
public:
  enum class State : uint8_t {
    Idle,    // not in a session
    Joining, // join() is under way
    Active   // hosting, or joined and connected to every peer
  };

  explicit Session(Network &network);
  ~Session();
  Session(Session &&) noexcept;
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  Session &operator=(Session &&) = delete;

  // This peer becomes id 0. False if the network is not open or already in a
  // session.
  bool host(const SessionConfig &config = SessionConfig());

  // The outcome arrives as onJoined / onJoinFailed. False if the network is not
  // open, already in a session, or the address does not parse.
  bool join(const Address &leader,
            const SessionConfig &config = SessionConfig());
  bool join(const std::string &text,
            const SessionConfig &config = SessionConfig());
  // Also checks it is still the same session.
  bool join(const LanSessionInfo &info,
            const SessionConfig &config = SessionConfig());

  // Across the internet the joined peer must punch toward the joiner: call this
  // with the joiner's public address as it calls join(). The leader announces
  // later joiners to the other peers itself.
  void expectJoiner(const Address &joinerPublicAddress) {
    m_network.punch(joinerPublicAddress);
  }

  // Leaves, telling the other peers. No callbacks fire for the leaving peer.
  void leave();

  State state() const;
  bool isActive() const {
    return state() == State::Active;
  }
  PeerId self() const {
    return m_shared->self;
  }
  PeerId leader() const {
    return m_shared->leader;
  }
  bool isLeader() const {
    return isActive() && m_shared->self == m_shared->leader;
  }
  uint32_t sessionId() const;
  std::vector<PeerId> peers() const;
  size_t peerCount() const {
    return m_shared->byId.size() + (m_shared->self == kNoPeer ? 0 : 1);
  }
  bool isPeer(PeerId peer) const {
    return m_shared->byId.count(peer) != 0;
  }
  // For diagnostics; messages are addressed by PeerId.
  Address addressOf(PeerId peer) const;
  PeerId peerOf(const Address &address) const;
  size_t maxReliableMessageSize() const {
    return m_network.config().maxMessageSize;
  }

  // Registers T on the Network. Do it on every peer before such messages can
  // arrive.
  template <typename T>
  bool registerMessage(Channel channel = Channel::Reliable) {
    return m_network.registerMessage<T>(channel);
  }
  // Called with the sender's id; messages from non-peers are dropped.
  template <typename T>
  bool onMessage(std::function<void(PeerId, const T &)> handler) {
    std::shared_ptr<Shared> shared = m_shared;
    return m_network.onMessage<T>(
        [shared, handler](const Address &from, const T &message) {
          auto it = shared->byAddress.find(from);
          if (it != shared->byAddress.end()) {
            handler(it->second, message);
          }
        });
  }

  template <typename T> bool send(PeerId peer, const T &message) {
    auto it = m_shared->byId.find(peer);
    return it != m_shared->byId.end() && m_network.send(it->second, message);
  }
  // Returns how many peers it was queued for.
  template <typename T> size_t broadcast(const T &message) {
    size_t queued = 0;
    for (const auto &entry : m_shared->byId) {
      queued += m_network.send(entry.second, message);
    }
    return queued;
  }
  template <typename T> bool sendToLeader(const T &message) {
    return send(m_shared->leader, message);
  }

  void onJoined(std::function<void()> handler);
  void onJoinFailed(std::function<void(JoinFailure)> handler);
  void onPeerJoined(std::function<void(PeerId)> handler);
  void onPeerLeft(std::function<void(PeerId, DisconnectReason)> handler);
  void onLeaderChanged(std::function<void(PeerId)> handler);

  // Timeouts, the unidentified-connection sweep and LAN advertising.
  void update(double now);

private:
  // Shared state, on the heap behind a shared_ptr so handlers survive a move.
  struct Shared {
    PeerId self = kNoPeer;
    PeerId leader = kNoPeer;
    std::unordered_map<Address, PeerId> byAddress;
    std::unordered_map<PeerId, Address> byId;
  };
  class Impl;

  Network &m_network;
  std::shared_ptr<Shared> m_shared;
  std::shared_ptr<Impl> m_impl;
};

} // namespace componeng::net
