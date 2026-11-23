#include "componeng/net/session.hpp"

#include "componeng/net/archive.hpp"
#include "componeng/net/archive_address.hpp"
#include "componeng/utils/logger.hpp"

#include <algorithm>
#include <map>
#include <random>

namespace componeng::net {

// Messages peers exchange to run a session (reliable, on the game's Network).
namespace session_msg {

struct JoinRequest {
  static constexpr const char *kName = "componeng.session.join_request";
  // 0 joins whatever session the peer runs; otherwise only that one.
  uint32_t sessionId = 0;
  template <typename Ar> void serialize(Ar &ar) {
    ar(sessionId);
  }
};

struct RosterEntry {
  PeerId id = kNoPeer;
  Address address;
  template <typename Ar> void serialize(Ar &ar) {
    ar(id, address);
  }
};

struct JoinAccept {
  static constexpr const char *kName = "componeng.session.join_accept";
  uint32_t sessionId = 0;
  PeerId yourId = kNoPeer;
  PeerId leaderId = kNoPeer;
  // The next id to hand out, which can be past every id still present.
  PeerId nextId = 0;
  uint16_t maxPeers = 0;
  std::string name;
  // Every peer but the leader and the joiner. The joiner connects to each.
  std::vector<RosterEntry> peers;
  template <typename Ar> void serialize(Ar &ar) {
    ar(sessionId, yourId, leaderId, nextId, maxPeers, name, peers);
  }
};

struct JoinReject {
  static constexpr const char *kName = "componeng.session.join_reject";
  JoinFailure reason = JoinFailure::Full;
  template <typename Ar> void serialize(Ar &ar) {
    ar(reason);
  }
};

// Sent by a joiner to each peer it connects to, and answered with the same.
struct Hello {
  static constexpr const char *kName = "componeng.session.hello";
  uint32_t sessionId = 0;
  PeerId id = kNoPeer;
  bool reply = false;
  template <typename Ar> void serialize(Ar &ar) {
    ar(sessionId, id, reply);
  }
};

// Leader to each peer already in: expect this joiner. Behind a NAT a peer only
// hears from addresses it has sent to, so peers must reach out as the joiner
// does.
struct ExpectJoiner {
  static constexpr const char *kName = "componeng.session.expect_joiner";
  uint32_t sessionId = 0;
  Address joiner;
  template <typename Ar> void serialize(Ar &ar) {
    ar(sessionId, joiner);
  }
};

} // namespace session_msg

namespace {

// "Stamp at the next update": the time is only known in Session::update, not
// inside Network::update.
constexpr double kStamp = -1.0;
// Connections that never identify themselves are dropped, so strangers cannot
// hold slots.
constexpr double kUnidentifiedGrace = 5.0;
constexpr size_t kMaxRosterEntries = 255;

} // namespace

class Session::Impl {
public:
  Impl(Network &network, std::shared_ptr<Shared> shared)
      : m_net(network), m_sh(std::move(shared)), m_rng(std::random_device{}()) {
  }

  // Wires the Network's callbacks to this. They hold only a weak reference, so
  // a Session that is gone leaves them harmless.
  void attach(const std::weak_ptr<Impl> &weak) {
    m_net.onPeerConnected([weak](const Address &a) {
      if (auto self = weak.lock()) {
        self->onConnected(a);
      }
    });
    m_net.onPeerDisconnected([weak](const Address &a, DisconnectReason r) {
      if (auto self = weak.lock()) {
        self->onDisconnected(a, r);
      }
    });
    m_net.onConnectFailed([weak](const Address &a, DisconnectReason) {
      if (auto self = weak.lock()) {
        self->onConnectFailed(a);
      }
    });

    constexpr auto kReliable = Channel::Reliable;
    bool ok = m_net.registerMessage<session_msg::JoinRequest>(kReliable) &&
              m_net.registerMessage<session_msg::JoinAccept>(kReliable) &&
              m_net.registerMessage<session_msg::JoinReject>(kReliable) &&
              m_net.registerMessage<session_msg::Hello>(kReliable) &&
              m_net.registerMessage<session_msg::ExpectJoiner>(kReliable);
    if (!ok) {
      LOG_ERROR("Could not register the session messages");
    }
    m_net.onMessage<session_msg::JoinRequest>(
        [weak](const Address &from, const session_msg::JoinRequest &m) {
          if (auto self = weak.lock()) {
            self->onJoinRequest(from, m);
          }
        });
    m_net.onMessage<session_msg::JoinAccept>(
        [weak](const Address &from, const session_msg::JoinAccept &m) {
          if (auto self = weak.lock()) {
            self->onJoinAccept(from, m);
          }
        });
    m_net.onMessage<session_msg::JoinReject>(
        [weak](const Address &from, const session_msg::JoinReject &m) {
          if (auto self = weak.lock()) {
            self->onJoinReject(from, m);
          }
        });
    m_net.onMessage<session_msg::Hello>(
        [weak](const Address &from, const session_msg::Hello &m) {
          if (auto self = weak.lock()) {
            self->onHello(from, m);
          }
        });
    m_net.onMessage<session_msg::ExpectJoiner>(
        [weak](const Address &from, const session_msg::ExpectJoiner &m) {
          if (auto self = weak.lock()) {
            self->onExpectJoiner(from, m);
          }
        });
  }

  State state() const {
    return m_state;
  }
  uint32_t sessionId() const {
    return m_sessionId;
  }

  bool host(const SessionConfig &config) {
    if (m_state != State::Idle || !m_net.isOpen()) {
      return false;
    }
    m_config = config;
    m_config.name = config.name.substr(0, kMaxSessionNameLength);
    m_sessionId = 0;
    while (m_sessionId == 0) {
      m_sessionId = m_rng();
    }
    m_maxPeers = std::max<uint16_t>(config.maxPeers, 1);
    m_sh->self = 0;
    m_sh->leader = 0;
    m_nextId = 1;
    m_state = State::Active;
    m_net.setAcceptIncoming(true);
    return true;
  }

  bool join(const Address &leader, const SessionConfig &config,
            uint32_t expectedSession) {
    if (m_state != State::Idle || !m_net.isOpen() || !leader.isValid()) {
      return false;
    }
    m_config = config;
    m_joinTarget = leader;
    m_expectedSession = expectedSession;
    m_joinRequestSent = false;
    m_joinStartedAt = kStamp;
    m_state = State::Joining;
    // Peers that join after this one connect to it.
    m_net.setAcceptIncoming(true);
    if (!m_net.connect(leader)) {
      reset();
      return false;
    }
    return true;
  }

  void leave() {
    if (m_state == State::Idle) {
      return;
    }
    disconnectEveryone();
    reset();
  }

  void update(double now) {
    m_now = now;
    if (m_state == State::Joining) {
      if (m_joinStartedAt == kStamp) {
        m_joinStartedAt = now;
      } else if (now - m_joinStartedAt >= m_config.joinTimeout) {
        fail(JoinFailure::Timeout);
      }
    }
    sweepUnidentified(now);
    updateAdvertising(now);
  }

  std::vector<std::function<void()>> onJoinedHandlers;
  std::vector<std::function<void(JoinFailure)>> onJoinFailedHandlers;
  std::vector<std::function<void(PeerId)>> onPeerJoinedHandlers;
  std::vector<std::function<void(PeerId, DisconnectReason)>> onPeerLeftHandlers;
  std::vector<std::function<void(PeerId)>> onLeaderChangedHandlers;

private:
  // ---- Network events ----

  void onConnected(const Address &address) {
    if (m_state == State::Idle) {
      return;
    }
    if (m_state == State::Joining && address == m_joinTarget &&
        !m_joinRequestSent) {
      m_joinRequestSent = true;
      m_net.send(address, session_msg::JoinRequest{m_expectedSession});
      return;
    }
    for (const auto &pending : m_meshPending) {
      if (pending.second == address) {
        m_net.send(address, session_msg::Hello{m_sessionId, m_sh->self, false});
        return;
      }
    }
    if (m_sh->byAddress.count(address) == 0) {
      // Someone who connected to us: expected to say who they are shortly.
      m_unidentified[address] = kStamp;
    }
  }

  void onConnectFailed(const Address &address) {
    if (m_state != State::Joining) {
      return;
    }
    if (address == m_joinTarget) {
      fail(JoinFailure::Unreachable);
      return;
    }
    for (const auto &pending : m_meshPending) {
      if (pending.second == address) {
        fail(JoinFailure::MeshFailed);
        return;
      }
    }
  }

  void onDisconnected(const Address &address, DisconnectReason reason) {
    m_unidentified.erase(address);
    if (m_state == State::Joining) {
      if (address == m_joinTarget) {
        fail(JoinFailure::Unreachable);
        return;
      }
      for (const auto &pending : m_meshPending) {
        if (pending.second == address) {
          fail(JoinFailure::MeshFailed);
          return;
        }
      }
    }
    auto it = m_sh->byAddress.find(address);
    if (it != m_sh->byAddress.end()) {
      removePeer(it->second, reason);
    }
  }

  // ---- Session messages ----

  void onJoinRequest(const Address &from, const session_msg::JoinRequest &m) {
    if (m_state != State::Active || m_sh->self != m_sh->leader) {
      m_net.send(from, session_msg::JoinReject{JoinFailure::NotLeader});
      return;
    }
    if (m.sessionId != 0 && m.sessionId != m_sessionId) {
      m_net.send(from, session_msg::JoinReject{JoinFailure::WrongSession});
      return;
    }
    // A request repeated by someone already in: answer with what they have.
    auto known = m_sh->byAddress.find(from);
    PeerId id = known != m_sh->byAddress.end() ? known->second : kNoPeer;
    if (id == kNoPeer) {
      if (peerCount() >= m_maxPeers) {
        m_net.send(from, session_msg::JoinReject{JoinFailure::Full});
        return;
      }
      id = m_nextId;
    }

    session_msg::JoinAccept accept;
    accept.sessionId = m_sessionId;
    accept.yourId = id;
    accept.leaderId = m_sh->self;
    accept.nextId = static_cast<PeerId>(std::max<uint32_t>(m_nextId, id + 1u));
    accept.maxPeers = m_maxPeers;
    accept.name = m_config.name;
    for (const auto &peer : sortedPeers()) {
      if (peer.first != id) {
        accept.peers.push_back({peer.first, peer.second});
      }
    }
    m_net.send(from, accept);
    if (known == m_sh->byAddress.end()) {
      // The others reach out to the newcomer while it reaches out to them.
      for (const auto &peer : sortedPeers()) {
        m_net.send(peer.second, session_msg::ExpectJoiner{m_sessionId, from});
      }
      addPeer(id, from);
    }
  }

  void onExpectJoiner(const Address &from, const session_msg::ExpectJoiner &m) {
    // Only the leader gets to point this peer at an address.
    auto leader = m_sh->byId.find(m_sh->leader);
    if (m_state == State::Idle || m.sessionId != m_sessionId ||
        leader == m_sh->byId.end() || leader->second != from ||
        !m.joiner.isValid()) {
      return;
    }
    m_net.punch(m.joiner);
  }

  void onJoinAccept(const Address &from, const session_msg::JoinAccept &m) {
    if (m_state != State::Joining || from != m_joinTarget ||
        m_sh->self != kNoPeer) {
      return;
    }
    // Trust the leader to be well formed, but not so far as to act on nonsense.
    bool sane = m.yourId != kNoPeer && m.leaderId != kNoPeer &&
                m.yourId != m.leaderId && m.maxPeers >= 2 &&
                m.peers.size() + 2 <= m.maxPeers &&
                m.peers.size() <= kMaxRosterEntries &&
                (m_expectedSession == 0 || m.sessionId == m_expectedSession);
    for (const auto &entry : m.peers) {
      sane = sane && entry.id != kNoPeer && entry.id != m.yourId &&
             entry.id != m.leaderId && entry.address.isValid();
    }
    if (!sane) {
      fail(JoinFailure::WrongSession);
      return;
    }

    m_sessionId = m.sessionId;
    m_maxPeers = m.maxPeers;
    m_config.name = m.name.substr(0, kMaxSessionNameLength);
    m_nextId = m.nextId;
    m_sh->self = m.yourId;
    m_sh->leader = m.leaderId;
    addPeer(m.leaderId, from);

    for (const auto &entry : m.peers) {
      m_meshPending[entry.id] = entry.address;
    }
    std::vector<Address> targets;
    for (const auto &pending : m_meshPending) {
      targets.push_back(pending.second);
    }
    for (const Address &target : targets) {
      if (!m_net.connect(target)) {
        fail(JoinFailure::MeshFailed);
        return;
      }
    }
    completeJoinIfReady();
  }

  void onJoinReject(const Address &from, const session_msg::JoinReject &m) {
    if (m_state == State::Joining && from == m_joinTarget) {
      fail(m.reason);
    }
  }

  void onHello(const Address &from, const session_msg::Hello &m) {
    if (m_state == State::Idle || m_sh->self == kNoPeer) {
      return;
    }
    if (m.sessionId != m_sessionId) {
      m_net.disconnect(from);
      return;
    }

    // An answer from a peer we connected to while joining.
    for (auto it = m_meshPending.begin(); it != m_meshPending.end(); ++it) {
      if (it->second == from) {
        if (!m.reply || m.id != it->first) {
          fail(JoinFailure::MeshFailed);
          return;
        }
        m_meshPending.erase(it);
        addPeer(m.id, from);
        completeJoinIfReady();
        return;
      }
    }

    if (m_sh->byAddress.count(from) != 0 || m.reply) {
      return;
    }
    // A peer that joined after us, introducing itself.
    if (m.id == kNoPeer || m.id == m_sh->self || m_sh->byId.count(m.id) != 0 ||
        peerCount() >= m_maxPeers) {
      m_net.disconnect(from);
      return;
    }
    addPeer(m.id, from);
    m_net.send(from, session_msg::Hello{m_sessionId, m_sh->self, true});
  }

  // ---- Roster ----

  std::map<PeerId, Address> sortedPeers() const {
    return std::map<PeerId, Address>(m_sh->byId.begin(), m_sh->byId.end());
  }
  size_t peerCount() const {
    return m_sh->byId.size() + 1;
  }

  void addPeer(PeerId id, const Address &address) {
    m_sh->byId[id] = address;
    m_sh->byAddress[address] = id;
    m_unidentified.erase(address);
    m_nextId = static_cast<PeerId>(std::max<uint32_t>(m_nextId, id + 1u));
    for (size_t i = 0; i < onPeerJoinedHandlers.size(); ++i) {
      onPeerJoinedHandlers[i](id);
    }
  }

  void removePeer(PeerId id, DisconnectReason reason) {
    auto it = m_sh->byId.find(id);
    if (it == m_sh->byId.end()) {
      return;
    }
    m_sh->byAddress.erase(it->second);
    m_sh->byId.erase(it);
    for (size_t i = 0; i < onPeerLeftHandlers.size(); ++i) {
      onPeerLeftHandlers[i](id, reason);
    }

    // The leader is the lowest id still here; it only changes when the leader
    // is the one that went.
    if (id == m_sh->leader) {
      PeerId next = m_sh->self;
      for (const auto &entry : m_sh->byId) {
        next = std::min(next, entry.first);
      }
      m_sh->leader = next;
      for (size_t i = 0; i < onLeaderChangedHandlers.size(); ++i) {
        onLeaderChangedHandlers[i](next);
      }
    }
  }

  void completeJoinIfReady() {
    if (m_state != State::Joining || !m_meshPending.empty()) {
      return;
    }
    m_state = State::Active;
    for (size_t i = 0; i < onJoinedHandlers.size(); ++i) {
      onJoinedHandlers[i]();
    }
  }

  // ---- Teardown ----

  void disconnectEveryone() {
    std::vector<Address> addresses;
    for (const auto &entry : m_sh->byId) {
      addresses.push_back(entry.second);
    }
    if (m_state == State::Joining) {
      addresses.push_back(m_joinTarget);
    }
    for (const auto &pending : m_meshPending) {
      addresses.push_back(pending.second);
    }
    for (const auto &entry : m_unidentified) {
      addresses.push_back(entry.first);
    }
    for (const Address &address : addresses) {
      m_net.disconnect(address);
    }
  }

  // Back to not being in a session, without telling anyone.
  void reset() {
    m_state = State::Idle;
    m_sh->self = kNoPeer;
    m_sh->leader = kNoPeer;
    m_sh->byId.clear();
    m_sh->byAddress.clear();
    m_meshPending.clear();
    m_unidentified.clear();
    m_sessionId = 0;
    m_joinTarget = Address();
    m_advertiser.stop();
    m_net.setAcceptIncoming(false);
  }

  void fail(JoinFailure reason) {
    if (m_state != State::Joining) {
      return;
    }
    disconnectEveryone();
    reset();
    for (size_t i = 0; i < onJoinFailedHandlers.size(); ++i) {
      onJoinFailedHandlers[i](reason);
    }
  }

  void sweepUnidentified(double now) {
    std::vector<Address> expired;
    for (auto &entry : m_unidentified) {
      if (entry.second == kStamp) {
        entry.second = now;
      } else if (now - entry.second >= kUnidentifiedGrace) {
        expired.push_back(entry.first);
      }
    }
    for (const Address &address : expired) {
      m_unidentified.erase(address);
      m_net.disconnect(address);
    }
  }

  void updateAdvertising(double now) {
    bool advertise = m_state == State::Active && m_sh->self == m_sh->leader &&
                     m_config.advertiseOnLan && m_net.isOpen();
    if (!advertise) {
      if (m_advertiser.isRunning()) {
        m_advertiser.stop();
      }
      return;
    }
    if (!m_advertiser.isRunning()) {
      m_advertiser.start(m_net.config().protocolId, m_config.discoveryPort,
                         m_config.discoverySockets);
    }
    LanAdvert advert;
    advert.sessionId = m_sessionId;
    advert.name = m_config.name;
    advert.gamePort = m_net.localAddress().port();
    advert.peers = static_cast<uint16_t>(peerCount());
    advert.maxPeers = m_maxPeers;
    m_advertiser.setAdvert(advert);
    m_advertiser.update(now);
  }

  Network &m_net;
  std::shared_ptr<Shared> m_sh;
  std::mt19937 m_rng;

  State m_state = State::Idle;
  SessionConfig m_config;
  uint32_t m_sessionId = 0;
  uint16_t m_maxPeers = 0;
  PeerId m_nextId = 0;
  double m_now = 0.0;

  // While joining.
  Address m_joinTarget;
  uint32_t m_expectedSession = 0;
  bool m_joinRequestSent = false;
  double m_joinStartedAt = kStamp;
  // Peers still to connect to, by the id the leader gave them.
  std::map<PeerId, Address> m_meshPending;

  // Connections not yet known to be joiners or peers, by when they appeared.
  std::unordered_map<Address, double> m_unidentified;
  LanAdvertiser m_advertiser;
};

Session::Session(Network &network)
    : m_network(network), m_shared(std::make_shared<Shared>()),
      m_impl(std::make_shared<Impl>(network, m_shared)) {
  m_impl->attach(m_impl);
}

Session::~Session() = default;
Session::Session(Session &&) noexcept = default;

bool Session::host(const SessionConfig &config) {
  return m_impl->host(config);
}

bool Session::join(const Address &leader, const SessionConfig &config) {
  return m_impl->join(leader, config, 0);
}

bool Session::join(const std::string &text, const SessionConfig &config) {
  Address leader;
  return Address::parse(text, leader) && m_impl->join(leader, config, 0);
}

bool Session::join(const LanSessionInfo &info, const SessionConfig &config) {
  return m_impl->join(info.address, config, info.sessionId);
}

void Session::leave() {
  m_impl->leave();
}

Session::State Session::state() const {
  return m_impl->state();
}

uint32_t Session::sessionId() const {
  return m_impl->sessionId();
}

std::vector<PeerId> Session::peers() const {
  std::vector<PeerId> ids;
  for (const auto &entry : m_shared->byId) {
    ids.push_back(entry.first);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

Address Session::addressOf(PeerId peer) const {
  auto it = m_shared->byId.find(peer);
  return it == m_shared->byId.end() ? Address() : it->second;
}

PeerId Session::peerOf(const Address &address) const {
  auto it = m_shared->byAddress.find(address);
  return it == m_shared->byAddress.end() ? kNoPeer : it->second;
}

void Session::onJoined(std::function<void()> handler) {
  m_impl->onJoinedHandlers.push_back(std::move(handler));
}

void Session::onJoinFailed(std::function<void(JoinFailure)> handler) {
  m_impl->onJoinFailedHandlers.push_back(std::move(handler));
}

void Session::onPeerJoined(std::function<void(PeerId)> handler) {
  m_impl->onPeerJoinedHandlers.push_back(std::move(handler));
}

void Session::onPeerLeft(
    std::function<void(PeerId, DisconnectReason)> handler) {
  m_impl->onPeerLeftHandlers.push_back(std::move(handler));
}

void Session::onLeaderChanged(std::function<void(PeerId)> handler) {
  m_impl->onLeaderChangedHandlers.push_back(std::move(handler));
}

void Session::update(double now) {
  m_impl->update(now);
}

} // namespace componeng::net
