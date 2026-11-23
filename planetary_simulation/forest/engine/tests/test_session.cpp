#include "componeng/core/engine.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/events/session_event.hpp"
#include "componeng/net/ecs_bridge.hpp"
#include "componeng/net/lan_discovery.hpp"
#include "componeng/net/loopback_socket.hpp"
#include "componeng/net/net_plugin.hpp"
#include "componeng/net/network.hpp"
#include "componeng/net/session.hpp"
#include "net_test_config.hpp"
#include "gtest/gtest.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace componeng::net;
using namespace componeng::events;

namespace {

struct PieceMoved {
  static constexpr const char *kName = "jigsaw.piece_moved";
  uint16_t piece = 0;
  float x = 0, y = 0;
  template <typename Ar> void serialize(Ar &ar) {
    ar(piece, x, y);
  }
};

struct Rig;

// One test-session peer and a log of what its session reported.
struct Member {
  Network net;
  Session session;
  std::vector<PeerId> peersJoined;
  std::vector<std::pair<PeerId, DisconnectReason>> peersLeft;
  std::vector<PeerId> leaders;
  std::vector<JoinFailure> failures;
  int joined = 0;
  std::vector<std::pair<PeerId, PieceMoved>> moves;
  bool paused = false;

  explicit Member(LoopbackNetwork &loopback) : session(net) {
    EXPECT_TRUE(net.open(loopback.open(), testConfig()));
    EXPECT_TRUE(session.registerMessage<PieceMoved>());
    session.onJoined([this] { ++joined; });
    session.onJoinFailed([this](JoinFailure f) { failures.push_back(f); });
    session.onPeerJoined([this](PeerId p) { peersJoined.push_back(p); });
    session.onPeerLeft(
        [this](PeerId p, DisconnectReason r) { peersLeft.emplace_back(p, r); });
    session.onLeaderChanged([this](PeerId p) { leaders.push_back(p); });
    session.onMessage<PieceMoved>(
        std::function<void(PeerId, const PieceMoved &)>(
            [this](PeerId from, const PieceMoved &m) {
              moves.emplace_back(from, m);
            }));
  }

  Address address() const {
    return Address::loopback(net.localAddress().port());
  }
  bool movedFrom(PeerId peer) const {
    for (const auto &move : moves) {
      if (move.first == peer) {
        return true;
      }
    }
    return false;
  }
};

// Several peers on one simulated network and clock, stepped the way the
// NetPlugin's system does: network first, then session.
struct Rig {
  LoopbackNetwork loopback;
  std::vector<std::unique_ptr<Member>> members;
  double now = 0.0;

  Member &add() {
    members.push_back(std::make_unique<Member>(loopback));
    return *members.back();
  }
  SessionConfig config(uint16_t maxPeers = 8) {
    SessionConfig c;
    c.name = "test";
    c.maxPeers = maxPeers;
    c.discoverySockets = [this](uint16_t port,
                                bool) -> std::unique_ptr<IDatagramSocket> {
      return loopback.open(port);
    };
    return c;
  }
  void step(double dt = 0.01) {
    now += dt;
    for (auto &m : members) {
      if (m->paused) {
        continue;
      }
      m->net.update(now);
      m->session.update(now);
    }
  }
  void run(double seconds) {
    for (double end = now + seconds; now < end;) {
      step();
    }
  }
  bool runUntil(const std::function<bool()> &done, double seconds = 10.0) {
    for (double end = now + seconds; now < end;) {
      if (done()) {
        return true;
      }
      step();
    }
    return done();
  }
  void join(Member &joiner, Member &leader) {
    ASSERT_TRUE(joiner.session.join(leader.address(), config()));
    ASSERT_TRUE(runUntil([&] { return joiner.session.isActive(); }));
    ASSERT_TRUE(
        runUntil([&] { return leader.session.isPeer(joiner.session.self()); }));
  }
  bool allMeshed() {
    size_t active = 0;
    for (auto &m : members) {
      active += m->session.isActive();
    }
    for (auto &m : members) {
      if (m->session.isActive() && m->session.peerCount() != active) {
        return false;
      }
    }
    return true;
  }
};

std::vector<PeerId> ids(std::initializer_list<PeerId> list) {
  return std::vector<PeerId>(list);
}

} // namespace

// ---- Hosting and joining ----

TEST(Session, HostStartsAsPeerZeroAndLeader) {
  Rig rig;
  Member &host = rig.add();
  EXPECT_EQ(host.session.state(), Session::State::Idle);
  EXPECT_EQ(host.session.self(), kNoPeer);
  ASSERT_TRUE(host.session.host(rig.config()));
  EXPECT_EQ(host.session.state(), Session::State::Active);
  EXPECT_EQ(host.session.self(), 0);
  EXPECT_EQ(host.session.leader(), 0);
  EXPECT_TRUE(host.session.isLeader());
  EXPECT_EQ(host.session.peerCount(), 1u);
  EXPECT_TRUE(host.session.peers().empty());
  EXPECT_NE(host.session.sessionId(), 0u);
  EXPECT_FALSE(host.session.host(rig.config()));
}

TEST(Session, HostingNeedsAnOpenNetwork) {
  Network closed;
  Session session(closed);
  EXPECT_FALSE(session.host());
  EXPECT_FALSE(session.join(Address::loopback(1)));
  EXPECT_EQ(session.state(), Session::State::Idle);
}

TEST(Session, SecondPeerJoinsTheHost) {
  Rig rig;
  Member &host = rig.add();
  Member &guest = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));
  rig.join(guest, host);

  EXPECT_EQ(guest.session.self(), 1);
  EXPECT_EQ(guest.session.leader(), 0);
  EXPECT_FALSE(guest.session.isLeader());
  EXPECT_EQ(guest.session.sessionId(), host.session.sessionId());
  EXPECT_EQ(guest.session.peers(), ids({0}));
  EXPECT_EQ(host.session.peers(), ids({1}));
  EXPECT_EQ(guest.joined, 1);
  EXPECT_EQ(guest.peersJoined, ids({0}));
  EXPECT_EQ(host.peersJoined, ids({1}));
  EXPECT_EQ(host.joined, 0); // the host never "joins"
  EXPECT_EQ(host.session.addressOf(1), guest.address());
  EXPECT_EQ(host.session.peerOf(guest.address()), 1);
  EXPECT_EQ(host.session.peerOf(Address::loopback(9)), kNoPeer);
}

TEST(Session, JoinersGetUniqueIdsAndAFullMesh) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  Member &d = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  rig.join(d, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));

  EXPECT_EQ(a.session.self(), 0);
  EXPECT_EQ(b.session.self(), 1);
  EXPECT_EQ(c.session.self(), 2);
  EXPECT_EQ(d.session.self(), 3);
  EXPECT_EQ(a.session.peers(), ids({1, 2, 3}));
  EXPECT_EQ(b.session.peers(), ids({0, 2, 3}));
  EXPECT_EQ(c.session.peers(), ids({0, 1, 3}));
  EXPECT_EQ(d.session.peers(), ids({0, 1, 2}));
  for (auto &m : rig.members) {
    EXPECT_EQ(m->session.leader(), 0);
    EXPECT_EQ(m->net.peers().size(), 3u); // one connection per other peer
  }
}

TEST(Session, PeersJoiningAtTheSameTimeFindEachOther) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  ASSERT_TRUE(b.session.join(a.address(), rig.config()));
  ASSERT_TRUE(c.session.join(a.address(), rig.config()));
  ASSERT_TRUE(rig.runUntil([&] {
    return b.session.isActive() && c.session.isActive() && rig.allMeshed();
  }));
  EXPECT_NE(b.session.self(), c.session.self());
  EXPECT_TRUE(b.session.isPeer(c.session.self()));
  EXPECT_TRUE(c.session.isPeer(b.session.self()));
  EXPECT_EQ(a.session.peerCount(), 3u);
}

// ---- Messages ----

TEST(Session, MessagesAreAddressedAndAttributedByPeerId) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  Member &d = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  rig.join(d, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));

  EXPECT_EQ(d.session.broadcast(PieceMoved{7, 1.5f, 2.5f}), 3u);
  ASSERT_TRUE(a.session.send(2, PieceMoved{9, 0, 0}));
  EXPECT_TRUE(d.session.sendToLeader(PieceMoved{11, 0, 0}));
  EXPECT_FALSE(a.session.sendToLeader(PieceMoved{})); // a is the leader
  EXPECT_FALSE(a.session.send(99, PieceMoved{}));     // no such peer
  EXPECT_FALSE(a.session.send(0, PieceMoved{}));      // not oneself
  rig.run(0.5);

  for (Member *m : {&a, &b, &c}) {
    bool found = false;
    for (const auto &move : m->moves) {
      found = found || (move.first == 3 && move.second.piece == 7 &&
                        move.second.x == 1.5f && move.second.y == 2.5f);
    }
    EXPECT_TRUE(found);
  }
  EXPECT_TRUE(c.movedFrom(0));
  EXPECT_FALSE(b.movedFrom(0));
  EXPECT_FALSE(d.movedFrom(0));
  bool toLeader = false;
  for (const auto &move : a.moves) {
    toLeader = toLeader || (move.first == 3 && move.second.piece == 11);
  }
  EXPECT_TRUE(toLeader);
}

TEST(Session, MessagesFromStrangersAreNotDelivered) {
  Rig rig;
  Member &host = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));

  // Connects to the host's port but never joins the session.
  Network stranger;
  ASSERT_TRUE(stranger.open(rig.loopback.open(), testConfig()));
  ASSERT_TRUE(stranger.registerMessage<PieceMoved>());
  ASSERT_TRUE(stranger.connect(host.address()));
  bool sent = false;
  for (int i = 0; i < 200; ++i) {
    rig.step();
    stranger.update(rig.now);
    if (!sent && stranger.isConnected(host.address())) {
      stranger.send(host.address(), PieceMoved{1, 2, 3});
      sent = true;
    }
  }
  ASSERT_TRUE(sent);
  EXPECT_TRUE(host.moves.empty());
  EXPECT_TRUE(host.session.peers().empty());
}

TEST(Session, StrangersAreDroppedAfterAGracePeriod) {
  Rig rig;
  Member &host = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));
  Network stranger;
  ASSERT_TRUE(stranger.open(rig.loopback.open(), testConfig()));
  bool dropped = false;
  stranger.onPeerDisconnected(
      [&](const Address &, DisconnectReason) { dropped = true; });
  ASSERT_TRUE(stranger.connect(host.address()));

  double connectedAt = -1;
  for (int i = 0; i < 1500 && !dropped; ++i) {
    rig.step();
    stranger.update(rig.now);
    if (connectedAt < 0 && stranger.isConnected(host.address())) {
      connectedAt = rig.now;
    }
  }
  ASSERT_GE(connectedAt, 0.0);
  EXPECT_TRUE(dropped);
  EXPECT_GE(rig.now - connectedAt, 4.0);
  EXPECT_LE(rig.now - connectedAt, 7.0);
  EXPECT_TRUE(host.net.peers().empty());
}

// ---- Refusals and failures ----

TEST(Session, FullSessionTurnsJoinersAway) {
  Rig rig;
  Member &host = rig.add();
  Member &first = rig.add();
  Member &late = rig.add();
  ASSERT_TRUE(host.session.host(rig.config(2)));
  rig.join(first, host);
  ASSERT_TRUE(late.session.join(host.address(), rig.config()));
  ASSERT_TRUE(rig.runUntil([&] { return !late.failures.empty(); }));

  EXPECT_EQ(late.failures, std::vector<JoinFailure>{JoinFailure::Full});
  EXPECT_EQ(late.session.state(), Session::State::Idle);
  EXPECT_EQ(late.session.self(), kNoPeer);
  EXPECT_EQ(host.session.peerCount(), 2u);
  rig.run(0.5);
  EXPECT_TRUE(late.net.peers().empty());
}

TEST(Session, OnlyTheLeaderAdmitsJoiners) {
  Rig rig;
  Member &host = rig.add();
  Member &member = rig.add();
  Member &outsider = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));
  rig.join(member, host);
  ASSERT_TRUE(outsider.session.join(member.address(), rig.config()));
  ASSERT_TRUE(rig.runUntil([&] { return !outsider.failures.empty(); }));
  EXPECT_EQ(outsider.failures,
            std::vector<JoinFailure>{JoinFailure::NotLeader});
  EXPECT_EQ(host.session.peerCount(), 2u);
}

TEST(Session, JoiningAnotherSessionByIdIsRefused) {
  Rig rig;
  Member &host = rig.add();
  Member &guest = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));
  LanSessionInfo info;
  info.address = host.address();
  info.sessionId = host.session.sessionId() + 1; // a stale or wrong listing
  ASSERT_TRUE(guest.session.join(info, rig.config()));
  ASSERT_TRUE(rig.runUntil([&] { return !guest.failures.empty(); }));
  EXPECT_EQ(guest.failures,
            std::vector<JoinFailure>{JoinFailure::WrongSession});

  info.sessionId = host.session.sessionId();
  ASSERT_TRUE(guest.session.join(info, rig.config()));
  ASSERT_TRUE(rig.runUntil([&] { return guest.session.isActive(); }));
}

TEST(Session, JoinFailsWhenNobodyAnswers) {
  Rig rig;
  Member &guest = rig.add();
  Member &gone = rig.add();
  Address nowhere = gone.address();
  gone.net.close();
  ASSERT_TRUE(guest.session.join(nowhere, rig.config()));
  EXPECT_EQ(guest.session.state(), Session::State::Joining);
  ASSERT_TRUE(rig.runUntil([&] { return !guest.failures.empty(); }));
  EXPECT_EQ(guest.failures, std::vector<JoinFailure>{JoinFailure::Unreachable});
  EXPECT_EQ(guest.session.state(), Session::State::Idle);
  EXPECT_TRUE(guest.session.join(nowhere, rig.config()));
}

TEST(Session, JoinGivesUpAfterItsTimeout) {
  Rig rig;
  Member &guest = rig.add();
  Member &frozen = rig.add();
  ASSERT_TRUE(frozen.session.host(rig.config()));
  frozen.paused = true; // there, but never answers
  SessionConfig config = rig.config();
  config.joinTimeout = 1.0;
  ASSERT_TRUE(guest.session.join(frozen.address(), config));
  rig.run(0.8);
  EXPECT_TRUE(guest.failures.empty());
  rig.run(0.5);
  EXPECT_EQ(guest.failures, std::vector<JoinFailure>{JoinFailure::Timeout});
}

TEST(Session, JoinFailsIfAnotherPeerCannotBeReached) {
  Rig rig;
  Member &host = rig.add();
  Member &frozen = rig.add();
  Member &joiner = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));
  rig.join(frozen, host);
  // Still on the host's list, but it no longer answers anyone.
  frozen.paused = true;

  ASSERT_TRUE(joiner.session.join(host.address(), rig.config()));
  ASSERT_TRUE(rig.runUntil([&] { return !joiner.failures.empty(); }, 12.0));
  EXPECT_EQ(joiner.failures, std::vector<JoinFailure>{JoinFailure::MeshFailed});
  EXPECT_EQ(joiner.session.state(), Session::State::Idle);
  EXPECT_EQ(joiner.joined, 0);
  ASSERT_TRUE(rig.runUntil([&] { return !host.session.isPeer(2); }, 2.0));
}

TEST(Session, CannotJoinWhileAlreadyInASession) {
  Rig rig;
  Member &host = rig.add();
  Member &guest = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));
  rig.join(guest, host);
  EXPECT_FALSE(guest.session.join(host.address(), rig.config()));
  EXPECT_FALSE(guest.session.join("not an address", rig.config()));
}

// ---- Leaving ----

TEST(Session, ALeavingPeerIsMissedByTheOthersButNotTheLeader) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));

  c.session.leave();
  EXPECT_EQ(c.session.state(), Session::State::Idle);
  ASSERT_TRUE(rig.runUntil([&] {
    return a.session.peerCount() == 2 && b.session.peerCount() == 2;
  }));
  ASSERT_EQ(a.peersLeft.size(), 1u);
  EXPECT_EQ(a.peersLeft[0].first, 2);
  EXPECT_EQ(a.peersLeft[0].second, DisconnectReason::Remote);
  EXPECT_EQ(b.peersLeft.size(), 1u);
  EXPECT_TRUE(a.leaders.empty());
  EXPECT_TRUE(b.leaders.empty());
  EXPECT_EQ(b.session.leader(), 0);
  EXPECT_TRUE(c.peersLeft.empty()); // leaving oneself is not an event
  EXPECT_TRUE(c.net.peers().empty());
}

TEST(Session, WhenTheLeaderLeavesTheLowestIdTakesOver) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));

  a.session.leave();
  ASSERT_TRUE(rig.runUntil(
      [&] { return b.session.leader() == 1 && c.session.leader() == 1; }));
  EXPECT_EQ(b.leaders, ids({1}));
  EXPECT_EQ(c.leaders, ids({1}));
  EXPECT_TRUE(b.session.isLeader());
  EXPECT_FALSE(c.session.isLeader());
  EXPECT_EQ(b.session.peers(), ids({2}));
  EXPECT_EQ(c.session.peers(), ids({1}));
  EXPECT_EQ(b.peersLeft[0].first, 0);

  // The session carries on: a newcomer joins through the new leader and gets
  // an id that nobody has held, not the departed host's.
  Member &d = rig.add();
  rig.join(d, b);
  EXPECT_EQ(d.session.self(), 3);
  EXPECT_EQ(d.session.leader(), 1);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));
  EXPECT_EQ(c.session.peers(), ids({1, 3}));
}

TEST(Session, ALeftPeerCanJoinAgainAsSomeoneNew) {
  Rig rig;
  Member &host = rig.add();
  Member &guest = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));
  rig.join(guest, host);
  PeerId first = guest.session.self();
  guest.session.leave();
  ASSERT_TRUE(rig.runUntil([&] { return host.session.peerCount() == 1; }));
  rig.join(guest, host);
  EXPECT_NE(guest.session.self(), first);
  EXPECT_EQ(guest.session.self(), 2);
}

TEST(Session, ASilentPeerTimesOut) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));

  c.paused = true; // crashes: no goodbye
  ASSERT_TRUE(rig.runUntil([&] { return a.session.peerCount() == 2; }, 15.0));
  ASSERT_EQ(a.peersLeft.size(), 1u);
  EXPECT_EQ(a.peersLeft[0].first, 2);
  EXPECT_EQ(a.peersLeft[0].second, DisconnectReason::Timeout);
}

// ---- Discovery ----

TEST(Session, LeaderAdvertisesAndTheAdvertTravelsWithLeadership) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &seeker = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);

  LanBrowser browser;
  ASSERT_TRUE(browser.start(a.net.config().protocolId, kDefaultDiscoveryPort,
                            rig.config().discoverySockets));
  auto find = [&](double seconds) {
    for (double end = rig.now + seconds; rig.now < end;) {
      rig.step();
      browser.update(rig.now);
    }
    return browser.sessions();
  };
  auto listed = find(2.0);
  ASSERT_EQ(listed.size(), 1u);
  EXPECT_EQ(listed[0].sessionId, a.session.sessionId());
  EXPECT_EQ(listed[0].name, "test");
  EXPECT_EQ(listed[0].peers, 2);
  EXPECT_EQ(listed[0].address, a.address());

  a.session.leave();
  listed = find(LanBrowser::kSessionExpiry + 2.0);
  ASSERT_EQ(listed.size(), 1u);
  EXPECT_EQ(listed[0].address, b.address());
  EXPECT_EQ(listed[0].peers, 1);

  ASSERT_TRUE(seeker.session.join(listed[0], rig.config()));
  ASSERT_TRUE(rig.runUntil([&] { return seeker.session.isActive(); }));
}

TEST(Session, AdvertisingCanBeTurnedOff) {
  Rig rig;
  Member &host = rig.add();
  SessionConfig config = rig.config();
  config.advertiseOnLan = false;
  ASSERT_TRUE(host.session.host(config));
  LanBrowser browser;
  ASSERT_TRUE(browser.start(host.net.config().protocolId, kDefaultDiscoveryPort,
                            config.discoverySockets));
  for (double end = rig.now + 3.0; rig.now < end;) {
    rig.step();
    browser.update(rig.now);
  }
  EXPECT_TRUE(browser.sessions().empty());
}

// ---- Lifetime ----

TEST(Session, SurvivesBeingMoved) {
  Rig rig;
  Member &host = rig.add();
  Member &guest = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));
  rig.join(guest, host);
  // The Member keeps its Session in place, so build a second one to move.
  Network net;
  ASSERT_TRUE(net.open(rig.loopback.open(), testConfig()));
  Session original(net);
  int joined = 0;
  original.onJoined([&] { ++joined; });
  Session moved(std::move(original));
  ASSERT_TRUE(moved.join(host.address(), rig.config()));
  for (int i = 0; i < 1000 && !moved.isActive(); ++i) {
    rig.step();
    net.update(rig.now);
    moved.update(rig.now);
  }
  EXPECT_TRUE(moved.isActive());
  EXPECT_EQ(joined, 1); // a handler added before the move still fires
  moved.leave();
}

TEST(Session, ADestroyedSessionLeavesTheNetworkHarmless) {
  Rig rig;
  Member &host = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));
  Network net;
  ASSERT_TRUE(net.open(rig.loopback.open(), testConfig()));
  {
    Session session(net);
    ASSERT_TRUE(session.join(host.address(), rig.config()));
    for (int i = 0; i < 500 && !session.isActive(); ++i) {
      rig.step();
      net.update(rig.now);
      session.update(rig.now);
    }
    ASSERT_TRUE(session.isActive());
  }
  // The network keeps running and delivering to handlers that no longer exist.
  net.disconnect(host.address());
  for (int i = 0; i < 300; ++i) {
    rig.step();
    net.update(rig.now);
  }
  SUCCEED();
}

// ---- ECS integration ----

TEST(Session, ReportsToTheWorldAsEvents) {
  LoopbackNetwork loopback;
  componeng::core::Engine engine;
  engine.add(NetPlugin{});
  engine.initialize();
  componeng::ecs::World &world = engine.world();

  auto &net = world.getResource<Network>();
  ASSERT_TRUE(net.open(loopback.open(), testConfig()));
  auto &session = world.getResource<Session>();
  ASSERT_TRUE(session.registerMessage<PieceMoved>());
  // Session lifecycle events come with the bridge; message events are opt in.
  ASSERT_TRUE(world.getResource<EcsBridge>().forward<PieceMoved>(session));
  SessionConfig config;
  config.advertiseOnLan = false;
  ASSERT_TRUE(session.host(config));

  Network remoteNet;
  ASSERT_TRUE(remoteNet.open(loopback.open(), testConfig()));
  Session remote(remoteNet);
  ASSERT_TRUE(remote.registerMessage<PieceMoved>());
  config.advertiseOnLan = false;
  ASSERT_TRUE(
      remote.join(Address::loopback(net.localAddress().port()), config));

  int joinedPeers = 0, leftPeers = 0, moves = 0;
  PeerId movePeer = kNoPeer;
  PieceMoved lastMove;
  bool sent = false;
  double now = 0.0;
  for (int frame = 0; frame < 400; ++frame) {
    now += 0.01;
    remoteNet.update(now);
    remote.update(now);
    if (remote.isActive() && !sent) {
      remote.send(0, PieceMoved{5, 0.5f, 0.25f});
      sent = true;
    }
    if (frame == 300) {
      remote.leave();
    }
    engine.update(0.01);
    joinedPeers +=
        static_cast<int>(world.getEvents<SessionPeerJoinedEvent>().size());
    leftPeers +=
        static_cast<int>(world.getEvents<SessionPeerLeftEvent>().size());
    for (const auto &e : world.getEvents<SessionMessageEvent<PieceMoved>>()) {
      ++moves;
      movePeer = e.from;
      lastMove = e.message;
    }
  }
  EXPECT_EQ(joinedPeers, 1);
  EXPECT_EQ(leftPeers, 1);
  EXPECT_EQ(moves, 1);
  EXPECT_EQ(movePeer, 1);
  EXPECT_EQ(lastMove.piece, 5);
  EXPECT_EQ(lastMove.x, 0.5f);
}

// ---- A whole session over real UDP, found by LAN search ----

TEST(Session, ThreePeersFindEachOtherOverRealUdp) {
  uint16_t discoveryPort = static_cast<uint16_t>(
      47950 +
      (std::chrono::steady_clock::now().time_since_epoch().count() % 40));
  SessionConfig config;
  config.name = "udp session";
  config.discoveryPort = discoveryPort;

  Network netA, netB, netC;
  Session a(netA), b(netB), c(netC);
  for (Network *net : {&netA, &netB, &netC}) {
    ASSERT_TRUE(net->open(0, testConfig()));
  }
  for (Session *session : {&a, &b, &c}) {
    ASSERT_TRUE(session->registerMessage<PieceMoved>());
  }
  ASSERT_TRUE(a.host(config));

  LanBrowser browserB, browserC;
  ASSERT_TRUE(browserB.start(netA.config().protocolId, discoveryPort));
  ASSERT_TRUE(browserC.start(netA.config().protocolId, discoveryPort));

  using Clock = std::chrono::steady_clock;
  auto start = Clock::now();
  auto now = [&] {
    return std::chrono::duration<double>(Clock::now() - start).count();
  };
  std::vector<PieceMoved> gotAtA;
  a.onMessage<PieceMoved>(std::function<void(PeerId, const PieceMoved &)>(
      [&](PeerId, const PieceMoved &m) { gotAtA.push_back(m); }));

  bool joinedB = false, joinedC = false, sent = false;
  while (now() < 10.0 && (gotAtA.size() < 2)) {
    double t = now();
    netA.update(t);
    a.update(t);
    netB.update(t);
    b.update(t);
    netC.update(t);
    c.update(t);
    browserB.update(t);
    browserC.update(t);
    if (!joinedB && !browserB.sessions().empty()) {
      joinedB = b.join(browserB.sessions()[0], config);
    }
    if (joinedB && b.isActive() && !joinedC && !browserC.sessions().empty()) {
      joinedC = c.join(browserC.sessions()[0], config);
    }
    if (c.isActive() && b.isActive() && !sent && b.peerCount() == 3 &&
        c.peerCount() == 3) {
      b.broadcast(PieceMoved{1, 1, 1});
      c.send(0, PieceMoved{2, 2, 2});
      sent = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_TRUE(a.isActive() && b.isActive() && c.isActive());
  EXPECT_EQ(a.peers(), ids({1, 2}));
  EXPECT_EQ(b.peers(), ids({0, 2}));
  EXPECT_EQ(c.peers(), ids({0, 1}));
  EXPECT_EQ(gotAtA.size(), 2u);
  a.leave();
  b.leave();
  c.leave();
}
