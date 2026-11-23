#include "componeng/net/loopback_socket.hpp"
#include "componeng/net/nat/invite_code.hpp"
#include "componeng/net/nat/nat_traversal.hpp"
#include "componeng/net/network.hpp"
#include "componeng/net/session.hpp"
#include "nat_sim.hpp"
#include "net_test_config.hpp"
#include "gtest/gtest.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace componeng::net;
using nat_sim::NatKind;

namespace {

struct Chat {
  static constexpr const char *kName = "test.chat";
  std::string text;
  template <typename Ar> void serialize(Ar &ar) {
    ar(text);
  }
};

// A peer behind a simulated router.
struct Peer {
  nat_sim::NatSocket *nat = nullptr; // owned by net
  Network net;
  NatTraversal traversal;
  Session session;
  std::vector<std::string> heard;
  std::vector<DisconnectReason> connectFailures;

  Peer() : traversal(net), session(net) {
  }

  Address publicAddress() const {
    return traversal.publicAddress();
  }
};

// Peers behind routers of chosen kinds, a pair of public STUN servers, and the
// internet between them, on one simulated clock.
struct World {
  LoopbackNetwork internet;
  nat_sim::MockStunServer stunOne{internet, 3478};
  nat_sim::MockStunServer stunTwo{internet, 3479};
  std::vector<std::unique_ptr<Peer>> peers;
  double now = 0.0;
  uint16_t nextPrivatePort = 6000;

  Peer &add(NatKind kind) {
    auto peer = std::make_unique<Peer>();
    auto nat =
        std::make_unique<nat_sim::NatSocket>(internet, kind, nextPrivatePort++);
    peer->nat = nat.get();
    EXPECT_TRUE(peer->net.open(std::move(nat), testConfig()));
    EXPECT_TRUE(peer->net.registerMessage<Chat>());
    Peer *raw = peer.get();
    peer->net.onMessage<Chat>([raw](const Address &, const Chat &m) {
      raw->heard.push_back(m.text);
    });
    peer->net.onConnectFailed([raw](const Address &, DisconnectReason r) {
      raw->connectFailures.push_back(r);
    });
    peers.push_back(std::move(peer));
    return *peers.back();
  }

  void step(double dt = 0.01) {
    now += dt;
    stunOne.update();
    stunTwo.update();
    for (auto &p : peers) {
      p->net.update(now);
      p->session.update(now);
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

  void discover() {
    for (auto &p : peers) {
      p->traversal.discoverPublicAddress({"127.0.0.1:3478", "127.0.0.1:3479"});
    }
    ASSERT_TRUE(runUntil([&] {
      for (auto &p : peers) {
        if (p->traversal.discoveryState() != NatDiscovery::State::Done) {
          return false;
        }
      }
      return true;
    }));
  }
};

} // namespace

// ---- What a peer learns about its router ----

TEST(NatTraversal, ThePublicAddressIsNotThePrivateOne) {
  World world;
  Peer &home = world.add(NatKind::PortRestricted);
  world.discover();
  EXPECT_TRUE(home.publicAddress().isValid());
  EXPECT_NE(home.publicAddress(), home.net.localAddress());
  EXPECT_EQ(home.net.localAddress().bytes()[0], 192); // the private side
  EXPECT_EQ(home.publicAddress().bytes()[0], 127);    // the "internet" side
}

TEST(NatTraversal, ConeAndSymmetricRoutersAreToldApart) {
  World world;
  Peer &full = world.add(NatKind::FullCone);
  Peer &restricted = world.add(NatKind::PortRestricted);
  Peer &symmetric = world.add(NatKind::Symmetric);
  world.discover();
  EXPECT_EQ(full.traversal.natType(), NatType::Cone);
  EXPECT_EQ(restricted.traversal.natType(), NatType::Cone);
  EXPECT_EQ(symmetric.traversal.natType(), NatType::Symmetric);
}

// ---- Getting through ----

TEST(NatTraversal, WithoutPunchingARestrictedRouterKeepsTheJoinerOut) {
  World world;
  Peer &host = world.add(NatKind::PortRestricted);
  Peer &joiner = world.add(NatKind::PortRestricted);
  world.discover();
  host.net.setAcceptIncoming(true);

  ASSERT_TRUE(joiner.net.connect(host.publicAddress()));
  world.run(7.0);
  EXPECT_FALSE(joiner.net.isConnected(host.publicAddress()));
  EXPECT_EQ(joiner.connectFailures,
            std::vector<DisconnectReason>{DisconnectReason::ConnectTimeout});
  EXPECT_GT(host.nat->dropped, 0);
}

TEST(NatTraversal, TheHostPunchingForTheJoinerLetsItIn) {
  World world;
  Peer &host = world.add(NatKind::PortRestricted);
  Peer &joiner = world.add(NatKind::PortRestricted);
  world.discover();
  host.net.setAcceptIncoming(true);

  Address joinersAddress;
  ASSERT_TRUE(decodeInvite(joiner.traversal.inviteCode(), joinersAddress));
  host.net.punch(joinersAddress);
  ASSERT_TRUE(joiner.traversal.connectToInvite(host.traversal.inviteCode()));
  ASSERT_TRUE(world.runUntil([&] {
    return joiner.net.isConnected(host.publicAddress()) &&
           host.net.isConnected(joiner.publicAddress());
  }));

  ASSERT_TRUE(
      joiner.net.send(host.publicAddress(), Chat{"hello through two routers"}));
  ASSERT_TRUE(host.net.send(joiner.publicAddress(), Chat{"and back"}));
  ASSERT_TRUE(world.runUntil(
      [&] { return !host.heard.empty() && !joiner.heard.empty(); }));
  EXPECT_EQ(host.heard[0], "hello through two routers");
  EXPECT_EQ(joiner.heard[0], "and back");
}

TEST(NatTraversal, BothPeersConnectingToEachOtherAtOnceWorksToo) {
  World world;
  Peer &a = world.add(NatKind::PortRestricted);
  Peer &b = world.add(NatKind::PortRestricted);
  world.discover();
  ASSERT_TRUE(a.traversal.connectToInvite(b.traversal.inviteCode()));
  ASSERT_TRUE(b.traversal.connectToInvite(a.traversal.inviteCode()));
  ASSERT_TRUE(world.runUntil([&] {
    return a.net.isConnected(b.publicAddress()) &&
           b.net.isConnected(a.publicAddress());
  }));
}

TEST(NatTraversal, AFullConeHostNeedsNoPunching) {
  World world;
  Peer &host = world.add(NatKind::FullCone);
  Peer &joiner = world.add(NatKind::PortRestricted);
  world.discover();
  host.net.setAcceptIncoming(true);
  ASSERT_TRUE(joiner.traversal.connectToInvite(host.traversal.inviteCode()));
  ASSERT_TRUE(world.runUntil(
      [&] { return joiner.net.isConnected(host.publicAddress()); }));
}

TEST(NatTraversal, ASymmetricJoinerCanStillReachAnOpenHost) {
  // What matters is that the host's address is the right one for the joiner to
  // send to; the joiner's own address is learned from the packets themselves.
  World world;
  Peer &host = world.add(NatKind::FullCone);
  Peer &joiner = world.add(NatKind::Symmetric);
  world.discover();
  host.net.setAcceptIncoming(true);
  ASSERT_TRUE(joiner.traversal.connectToInvite(host.traversal.inviteCode()));
  ASSERT_TRUE(world.runUntil([&] {
    return joiner.net.isConnected(host.publicAddress()) &&
           host.net.peers().size() == 1;
  }));
}

TEST(NatTraversal, ASymmetricHostCannotBeReachedEvenWithPunching) {
  // The address STUN gave the host is only good for the STUN server: the host's
  // router uses another one toward the joiner.
  World world;
  Peer &host = world.add(NatKind::Symmetric);
  Peer &joiner = world.add(NatKind::PortRestricted);
  world.discover();
  ASSERT_EQ(host.traversal.natType(), NatType::Symmetric);
  host.net.setAcceptIncoming(true);
  host.net.punch(joiner.publicAddress(), 20.0);
  ASSERT_TRUE(joiner.traversal.connectToInvite(host.traversal.inviteCode()));
  world.run(8.0);
  EXPECT_FALSE(joiner.net.isConnected(host.publicAddress()));
  EXPECT_EQ(joiner.connectFailures.size(), 1u);
}

TEST(NatTraversal, TwoSymmetricPeersCannotReachEachOther) {
  World world;
  Peer &a = world.add(NatKind::Symmetric);
  Peer &b = world.add(NatKind::Symmetric);
  world.discover();
  ASSERT_TRUE(a.traversal.connectToInvite(b.traversal.inviteCode()));
  ASSERT_TRUE(b.traversal.connectToInvite(a.traversal.inviteCode()));
  world.run(8.0);
  EXPECT_FALSE(a.net.isConnected(b.publicAddress()));
  EXPECT_FALSE(b.net.isConnected(a.publicAddress()));
}

// ---- A whole session through routers ----

TEST(NatTraversal, ASessionOfThreeHomesFormsAFullMesh) {
  World world;
  Peer &host = world.add(NatKind::PortRestricted);
  Peer &first = world.add(NatKind::PortRestricted);
  Peer &second = world.add(NatKind::PortRestricted);
  world.discover();
  for (Peer *p : {&host, &first, &second}) {
    ASSERT_TRUE(p->session.registerMessage<Chat>());
  }
  SessionConfig config;
  config.advertiseOnLan = false;
  ASSERT_TRUE(host.session.host(config));

  // The first joiner: codes swapped, host punches, joiner joins.
  Address address;
  ASSERT_TRUE(decodeInvite(first.traversal.inviteCode(), address));
  host.session.expectJoiner(address);
  ASSERT_TRUE(decodeInvite(host.traversal.inviteCode(), address));
  ASSERT_TRUE(first.session.join(address, config));
  ASSERT_TRUE(world.runUntil([&] { return first.session.isActive(); }));

  // The second does the same with the host; the leader then introduces it to
  // the first, which punches toward it without being asked by a person.
  ASSERT_TRUE(decodeInvite(second.traversal.inviteCode(), address));
  host.session.expectJoiner(address);
  ASSERT_TRUE(decodeInvite(host.traversal.inviteCode(), address));
  ASSERT_TRUE(second.session.join(address, config));
  ASSERT_TRUE(world.runUntil([&] {
    return second.session.isActive() && host.session.peerCount() == 3 &&
           first.session.peerCount() == 3;
  }));

  EXPECT_EQ(host.session.peers(), (std::vector<PeerId>{1, 2}));
  EXPECT_EQ(first.session.peers(), (std::vector<PeerId>{0, 2}));
  EXPECT_EQ(second.session.peers(), (std::vector<PeerId>{0, 1}));

  std::vector<std::string> atFirst;
  first.session.onMessage<Chat>(std::function<void(PeerId, const Chat &)>(
      [&](PeerId from, const Chat &m) {
        atFirst.push_back(std::to_string(from) + ":" + m.text);
      }));
  ASSERT_TRUE(second.session.send(1, Chat{"hi from behind another router"}));
  ASSERT_TRUE(world.runUntil([&] { return !atFirst.empty(); }));
  EXPECT_EQ(atFirst[0], "2:hi from behind another router");
}

// ---- The real thing, when asked for ----

// Talks to Google's and Cloudflare's public STUN servers over the real
// internet, so it only runs when COMPONENG_REAL_STUN is set.
TEST(NatTraversal, RealStunServersAnswer) {
  if (!std::getenv("COMPONENG_REAL_STUN")) {
    GTEST_SKIP() << "set COMPONENG_REAL_STUN=1 to try the real public servers";
  }
  Network net;
  NatTraversal traversal(net);
  ASSERT_TRUE(net.open(0, testConfig()));
  bool fired = false, ok = false;
  NatInfo info;
  traversal.onDiscovery([&](bool success, const NatInfo &result) {
    fired = true;
    ok = success;
    info = result;
  });
  traversal.discoverPublicAddress();

  using Clock = std::chrono::steady_clock;
  auto start = Clock::now();
  auto now = [&] {
    return std::chrono::duration<double>(Clock::now() - start).count();
  };
  while (now() < 15.0 && !fired) {
    net.update(now());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ASSERT_TRUE(fired);
  ASSERT_TRUE(ok);
  EXPECT_TRUE(traversal.publicAddress().isValid());
  EXPECT_FALSE(traversal.inviteCode().empty());
  std::cout << "public address: " << traversal.publicAddress().toString()
            << "  invite code: " << traversal.inviteCode() << "  nat: "
            << (traversal.natType() == NatType::Cone        ? "cone"
                : traversal.natType() == NatType::Symmetric ? "symmetric"
                                                            : "unknown")
            << "\n";
}
