#include "componeng/core/engine.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/events/network_event.hpp"
#include "componeng/net/archive.hpp"
#include "componeng/net/archive_math.hpp"
#include "componeng/net/ecs_bridge.hpp"
#include "componeng/net/loopback_socket.hpp"
#include "componeng/net/message_registry.hpp"
#include "componeng/net/net_plugin.hpp"
#include "componeng/net/network.hpp"
#include "componeng/net/session.hpp"
#include "net_test_config.hpp"
#include "gtest/gtest.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace componeng::net;
using namespace componeng::events;
using Bytes = std::vector<uint8_t>;

namespace {

struct MoveMessage {
  static constexpr const char *kName = "chess.move";
  uint8_t from = 0;
  uint8_t to = 0;
  uint8_t promotion = 0;
  template <typename Ar> void serialize(Ar &ar) {
    ar(from, to, promotion);
  }
};

struct ChatMessage {
  static constexpr const char *kName = "chess.chat";
  std::string text;
  template <typename Ar> void serialize(Ar &ar) {
    ar(text);
  }
};

struct CursorMessage {
  static constexpr const char *kName = "jigsaw.cursor";
  componeng::core::Vec2 position;
  template <typename Ar> void serialize(Ar &ar) {
    ar(position);
  }
};

// Two names with the same id (found by search, not chosen to look alike).
struct ClashA {
  static constexpr const char *kName = "msg.131";
  template <typename Ar> void serialize(Ar &) {
  }
};
struct ClashB {
  static constexpr const char *kName = "msg.348";
  template <typename Ar> void serialize(Ar &) {
  }
};

struct NeverRegistered {
  static constexpr const char *kName = "never.registered";
  template <typename Ar> void serialize(Ar &) {
  }
};

// Networks on one simulated LoopbackNetwork and clock.
struct Rig {
  LoopbackNetwork loopback;
  std::vector<std::unique_ptr<Network>> nets;
  double now = 0.0;

  Network &add(bool accept = true) {
    auto net = std::make_unique<Network>();
    EXPECT_TRUE(net->open(loopback.open(), testConfig()));
    net->setAcceptIncoming(accept);
    nets.push_back(std::move(net));
    return *nets.back();
  }
  void step(double dt = 0.01) {
    now += dt;
    for (auto &net : nets) {
      net->update(now);
    }
  }
  void run(double seconds) {
    for (double end = now + seconds; now < end;) {
      step();
    }
  }
  bool runUntil(const std::function<bool()> &done, double seconds = 5.0) {
    for (double end = now + seconds; now < end;) {
      if (done()) {
        return true;
      }
      step();
    }
    return done();
  }
  // Waits for both ends (the accepting side learns a packet later).
  void connect(Network &from, Network &to) {
    Address toAddress = Address::loopback(to.localAddress().port());
    Address fromAddress = Address::loopback(from.localAddress().port());
    ASSERT_TRUE(from.connect(toAddress));
    ASSERT_TRUE(runUntil([&] {
      return from.isConnected(toAddress) && to.isConnected(fromAddress);
    }));
  }
};

} // namespace

// ---- MessageRegistry ----

TEST(MessageRegistry, IdsAreStableHashesOfTheName) {
  // Pinned: the id is what is on the wire, so it must not drift between builds.
  EXPECT_EQ(MessageRegistry::idOf("chess.move"), 0xC71B);
  EXPECT_EQ(MessageRegistry::idOf("a"), 52512);
  EXPECT_NE(MessageRegistry::idOf("chess.move"),
            MessageRegistry::idOf("chess.chat"));
}

TEST(MessageRegistry, EncodesTheIdThenTheFields) {
  MessageRegistry registry;
  ASSERT_TRUE(registry.add<MoveMessage>());
  ByteWriter writer;
  Channel channel = Channel::Unreliable;
  ASSERT_TRUE(registry.encode(MoveMessage{12, 28, 0}, writer, channel));
  EXPECT_EQ(channel, Channel::Reliable);
  EXPECT_EQ(writer.data(), (Bytes{0x1B, 0xC7, 12, 28, 0}));
}

TEST(MessageRegistry, DecodeCallsEveryHandlerInOrder) {
  MessageRegistry registry;
  registry.add<MoveMessage>();
  std::vector<int> calls;
  Address seenFrom;
  MoveMessage seen;
  registry.onReceive<MoveMessage>(
      [&](const Address &from, const MoveMessage &m) {
        calls.push_back(1);
        seenFrom = from;
        seen = m;
      });
  registry.onReceive<MoveMessage>(
      [&](const Address &, const MoveMessage &) { calls.push_back(2); });

  ByteWriter writer;
  Channel channel;
  registry.encode(MoveMessage{8, 16, 3}, writer, channel);
  Address sender = Address::ipv4(1, 2, 3, 4, 5);
  EXPECT_EQ(
      registry.decode(sender, channel, writer.data().data(), writer.size()),
      MessageRegistry::DecodeResult::Ok);
  EXPECT_EQ(calls, (std::vector<int>{1, 2}));
  EXPECT_EQ(seenFrom, sender);
  EXPECT_EQ(seen.from, 8);
  EXPECT_EQ(seen.to, 16);
  EXPECT_EQ(seen.promotion, 3);
}

TEST(MessageRegistry, RejectsWhatItCannotUse) {
  MessageRegistry registry;
  registry.add<MoveMessage>(Channel::Reliable);
  int calls = 0;
  registry.onReceive<MoveMessage>(
      [&](const Address &, const MoveMessage &) { ++calls; });
  Address from = Address::loopback(1);
  using R = MessageRegistry::DecodeResult;

  Bytes good = {0x1B, 0xC7, 1, 2, 3};
  EXPECT_EQ(registry.decode(from, Channel::Reliable, good.data(), good.size()),
            R::Ok);
  EXPECT_EQ(calls, 1);

  // Right bytes, wrong channel: a peer that sends a move unreliably is broken.
  EXPECT_EQ(
      registry.decode(from, Channel::Unreliable, good.data(), good.size()),
      R::WrongChannel);
  Bytes unknown = {0x00, 0x00, 1, 2, 3};
  EXPECT_EQ(
      registry.decode(from, Channel::Reliable, unknown.data(), unknown.size()),
      R::UnknownType);
  Bytes truncated = {0x1B, 0xC7, 1, 2};
  EXPECT_EQ(registry.decode(from, Channel::Reliable, truncated.data(),
                            truncated.size()),
            R::Malformed);
  Bytes trailing = {0x1B, 0xC7, 1, 2, 3, 4};
  EXPECT_EQ(registry.decode(from, Channel::Reliable, trailing.data(),
                            trailing.size()),
            R::Malformed);
  Bytes tooShortForAnId = {0x1B};
  EXPECT_EQ(registry.decode(from, Channel::Reliable, tooShortForAnId.data(), 1),
            R::Malformed);
  EXPECT_EQ(registry.decode(from, Channel::Reliable, nullptr, 0), R::Malformed);
  EXPECT_EQ(calls, 1);
}

TEST(MessageRegistry, ClashingIdsAreCaughtWhenTheSecondIsAdded) {
  ASSERT_EQ(MessageRegistry::idOf(ClashA::kName),
            MessageRegistry::idOf(ClashB::kName));
  MessageRegistry registry;
  EXPECT_TRUE(registry.add<ClashA>());
  EXPECT_FALSE(registry.add<ClashB>());
  EXPECT_TRUE(registry.contains<ClashA>());
  EXPECT_FALSE(registry.contains<ClashB>());
}

TEST(MessageRegistry, AddingTwiceIsFineOnlyForTheSameChannel) {
  MessageRegistry registry;
  EXPECT_TRUE(registry.add<MoveMessage>(Channel::Reliable));
  EXPECT_TRUE(registry.add<MoveMessage>(Channel::Reliable));
  EXPECT_FALSE(registry.add<MoveMessage>(Channel::Unreliable));
}

TEST(MessageRegistry, UnregisteredTypesCannotBeEncodedOrHandled) {
  MessageRegistry registry;
  ByteWriter writer;
  Channel channel;
  EXPECT_FALSE(registry.encode(NeverRegistered{}, writer, channel));
  EXPECT_FALSE(registry.onReceive<NeverRegistered>(
      [](const Address &, const NeverRegistered &) {}));
}

// ---- Network ----

TEST(Network, SendsTypedMessagesBetweenPeers) {
  Rig rig;
  Network &host = rig.add();
  Network &guest = rig.add(false);
  for (Network *net : {&host, &guest}) {
    ASSERT_TRUE(net->registerMessage<MoveMessage>(Channel::Reliable));
    ASSERT_TRUE(net->registerMessage<CursorMessage>(Channel::Unreliable));
  }

  std::vector<MoveMessage> hostGot;
  std::vector<Address> hostFrom;
  host.onMessage<MoveMessage>([&](const Address &from, const MoveMessage &m) {
    hostFrom.push_back(from);
    hostGot.push_back(m);
  });
  std::vector<CursorMessage> guestGot;
  guest.onMessage<CursorMessage>(
      [&](const Address &, const CursorMessage &m) { guestGot.push_back(m); });

  rig.connect(guest, host);
  Address hostAddress = Address::loopback(host.localAddress().port());
  ASSERT_TRUE(guest.send(hostAddress, MoveMessage{12, 28, 0}));
  ASSERT_TRUE(guest.send(hostAddress, MoveMessage{52, 36, 0}));
  Address guestAddress = host.peers().at(0);
  ASSERT_TRUE(host.send(guestAddress, CursorMessage{{0.25f, 0.75f}}));
  rig.run(0.5);

  ASSERT_EQ(hostGot.size(), 2u);
  EXPECT_EQ(hostGot[0].from, 12);
  EXPECT_EQ(hostGot[1].to, 36);
  EXPECT_EQ(hostFrom[0], guestAddress);
  ASSERT_EQ(guestGot.size(), 1u);
  EXPECT_EQ(guestGot[0].position.x, 0.25f);
  EXPECT_EQ(host.rejectedMessages(), 0u);
  EXPECT_EQ(guest.rejectedMessages(), 0u);
}

TEST(Network, ConnectionCallbacksFire) {
  Rig rig;
  Network &host = rig.add();
  Network &guest = rig.add(false);
  int hostConnected = 0, guestConnected = 0;
  std::vector<DisconnectReason> hostLost;
  host.onPeerConnected([&](const Address &) { ++hostConnected; });
  guest.onPeerConnected([&](const Address &) { ++guestConnected; });
  host.onPeerDisconnected(
      [&](const Address &, DisconnectReason r) { hostLost.push_back(r); });

  rig.connect(guest, host);
  rig.run(0.2);
  EXPECT_EQ(hostConnected, 1);
  EXPECT_EQ(guestConnected, 1);

  guest.disconnect(Address::loopback(host.localAddress().port()));
  rig.run(0.2);
  ASSERT_EQ(hostLost.size(), 1u);
  EXPECT_EQ(hostLost[0], DisconnectReason::Remote);
  EXPECT_TRUE(host.peers().empty());
}

TEST(Network, ConnectFailedCallbackFires) {
  Rig rig;
  Network &lonely = rig.add(false);
  Network &closed = rig.add(false);
  std::vector<DisconnectReason> reasons;
  lonely.onConnectFailed(
      [&](const Address &, DisconnectReason r) { reasons.push_back(r); });
  ASSERT_TRUE(lonely.connect(Address::loopback(closed.localAddress().port())));
  rig.run(1.0);
  ASSERT_EQ(reasons.size(), 1u);
  EXPECT_EQ(reasons[0], DisconnectReason::Rejected);
}

TEST(Network, BroadcastReachesEveryConnectedPeer) {
  Rig rig;
  Network &hub = rig.add();
  std::vector<Network *> others;
  std::vector<int> received(3, 0);
  hub.registerMessage<ChatMessage>();
  for (int i = 0; i < 3; ++i) {
    Network &other = rig.add(false);
    other.registerMessage<ChatMessage>();
    other.onMessage<ChatMessage>(
        [&received, i](const Address &, const ChatMessage &m) {
          if (m.text == "hello all") {
            ++received[i];
          }
        });
    others.push_back(&other);
    rig.connect(other, hub);
  }
  EXPECT_EQ(hub.broadcast(ChatMessage{"hello all"}), 3u);
  rig.run(0.5);
  EXPECT_EQ(received, (std::vector<int>{1, 1, 1}));
}

TEST(Network, SendFailsForUnregisteredMessagesAndMissingPeers) {
  Rig rig;
  Network &a = rig.add();
  Network &b = rig.add(false);
  a.registerMessage<MoveMessage>();
  b.registerMessage<MoveMessage>();
  Address aAddress = Address::loopback(a.localAddress().port());
  EXPECT_FALSE(b.send(aAddress, MoveMessage{})); // not connected yet
  rig.connect(b, a);
  EXPECT_TRUE(b.send(aAddress, MoveMessage{}));
  EXPECT_FALSE(b.send(aAddress, NeverRegistered{}));
  EXPECT_EQ(b.broadcast(NeverRegistered{}), 0u);

  Network closed;
  EXPECT_FALSE(closed.send(aAddress, MoveMessage{}));
  EXPECT_EQ(closed.broadcast(MoveMessage{}), 0u);
  EXPECT_FALSE(closed.connect(aAddress));
}

TEST(Network, MessagesTheReceiverDoesNotKnowAreCountedNotDelivered) {
  Rig rig;
  Network &a = rig.add();
  Network &b = rig.add(false);
  a.registerMessage<MoveMessage>();
  b.registerMessage<MoveMessage>();
  b.registerMessage<ChatMessage>(); // a does not know chat
  int moves = 0;
  a.onMessage<MoveMessage>(
      [&](const Address &, const MoveMessage &) { ++moves; });
  rig.connect(b, a);

  Address aAddress = Address::loopback(a.localAddress().port());
  b.send(aAddress, ChatMessage{"anyone there?"});
  b.send(aAddress, MoveMessage{1, 2, 3});
  rig.run(0.5);
  EXPECT_EQ(moves, 1);
  EXPECT_EQ(a.rejectedMessages(), 1u);
  EXPECT_TRUE(a.isConnected(a.peers().at(0)));
}

TEST(Network, ChannelMismatchBetweenPeersIsRejected) {
  Rig rig;
  Network &a = rig.add();
  Network &b = rig.add(false);
  a.registerMessage<MoveMessage>(Channel::Reliable);
  b.registerMessage<MoveMessage>(Channel::Unreliable); // disagrees
  int moves = 0;
  a.onMessage<MoveMessage>(
      [&](const Address &, const MoveMessage &) { ++moves; });
  rig.connect(b, a);
  b.send(Address::loopback(a.localAddress().port()), MoveMessage{});
  rig.run(0.5);
  EXPECT_EQ(moves, 0);
  EXPECT_EQ(a.rejectedMessages(), 1u);
}

TEST(Network, ClosingFromAHandlerIsSafe) {
  Rig rig;
  Network &a = rig.add();
  Network &b = rig.add(false);
  a.onPeerConnected([&](const Address &) { a.close(); });
  ASSERT_TRUE(b.connect(Address::loopback(a.localAddress().port())));
  rig.run(0.5);
  EXPECT_FALSE(a.isOpen());
}

TEST(Network, CanBeMovedAfterRegistering) {
  Network original;
  ASSERT_TRUE(original.registerMessage<MoveMessage>());
  Network moved(std::move(original));
  EXPECT_TRUE(moved.registerMessage<MoveMessage>());
  EXPECT_FALSE(moved.isOpen());
}

// ---- ECS integration ----

TEST(NetPlugin, TurnsNetworkActivityIntoWorldEvents) {
  LoopbackNetwork loopback;
  componeng::core::Engine engine;
  engine.add(NetPlugin{});
  engine.initialize();
  componeng::ecs::World &world = engine.world();

  auto &net = world.getResource<Network>();
  ASSERT_TRUE(net.registerMessage<MoveMessage>());
  // Message events are opt in per type; peer events come with the bridge.
  ASSERT_TRUE(world.getResource<EcsBridge>().forward<MoveMessage>(net));
  ASSERT_TRUE(net.open(loopback.open(), testConfig()));
  net.setAcceptIncoming(true);

  Network remote;
  ASSERT_TRUE(remote.registerMessage<MoveMessage>());
  ASSERT_TRUE(remote.open(loopback.open(), testConfig()));
  Address worldAddress = Address::loopback(net.localAddress().port());
  ASSERT_TRUE(remote.connect(worldAddress));

  int connected = 0, moves = 0, lost = 0;
  MoveMessage lastMove;
  Address remoteAddress;
  bool sentMove = false;
  double now = 0.0;
  for (int frame = 0; frame < 300; ++frame) {
    now += 0.01;
    remote.update(now);
    if (remote.isConnected(worldAddress) && !sentMove) {
      remote.send(worldAddress, MoveMessage{4, 28, 0});
      sentMove = true;
    }
    if (frame == 200) {
      remote.disconnect(worldAddress);
    }
    engine.update(0.01);
    // Events live for one frame, so they are tallied as they appear.
    connected += static_cast<int>(world.getEvents<PeerConnectedEvent>().size());
    lost += static_cast<int>(world.getEvents<PeerDisconnectedEvent>().size());
    for (const auto &event :
         world.getEvents<MessageReceivedEvent<MoveMessage>>()) {
      ++moves;
      lastMove = event.message;
      remoteAddress = event.from;
    }
  }
  EXPECT_EQ(connected, 1);
  EXPECT_EQ(moves, 1);
  EXPECT_EQ(lost, 1);
  EXPECT_EQ(lastMove.from, 4);
  EXPECT_EQ(lastMove.to, 28);
  EXPECT_TRUE(remoteAddress.isValid());
}

TEST(NetPlugin, AddsTheResourcesOnce) {
  componeng::core::Engine engine;
  EXPECT_FALSE(engine.world().hasResource<Network>());
  engine.add(NetPlugin{});
  EXPECT_TRUE(engine.has<NetPlugin>());
  EXPECT_TRUE(engine.world().hasResource<Network>());
  EXPECT_TRUE(engine.world().hasResource<Session>());
  EXPECT_TRUE(engine.world().hasResource<LanBrowser>());
  EXPECT_TRUE(engine.world().hasResource<EcsBridge>());
  EXPECT_THROW(engine.add(NetPlugin{}), std::runtime_error);
  engine.initialize();
  engine.update(0.01);
  // Nothing is opened until the game asks.
  EXPECT_FALSE(engine.world().getResource<Network>().isOpen());
}

TEST(NetPlugin, ClosesTheNetworkAndLeavesTheSessionWhenTheEngineShutsDown) {
  LoopbackNetwork loopback;
  componeng::core::Engine engine;
  engine.add(NetPlugin{});
  engine.initialize();
  componeng::ecs::World &world = engine.world();
  auto &net = world.getResource<Network>();
  ASSERT_TRUE(net.open(loopback.open(), testConfig()));
  SessionConfig config;
  config.advertiseOnLan = false;
  ASSERT_TRUE(world.getResource<Session>().host(config));

  // A guest joins, then the world shuts down: the guest is told.
  Network guestNet;
  ASSERT_TRUE(guestNet.open(loopback.open(), testConfig()));
  Session guest(guestNet);
  ASSERT_TRUE(guest.join(Address::loopback(net.localAddress().port()), config));
  bool left = false;
  guest.onPeerLeft([&](PeerId, DisconnectReason) { left = true; });
  double now = 0.0;
  for (int i = 0; i < 300 && !guest.isActive(); ++i) {
    now += 0.01;
    engine.update(0.01);
    guestNet.update(now);
    guest.update(now);
  }
  ASSERT_TRUE(guest.isActive());
  engine.shutdown();
  EXPECT_FALSE(net.isOpen());
  for (int i = 0; i < 100 && !left; ++i) {
    now += 0.01;
    guestNet.update(now);
    guest.update(now);
  }
  EXPECT_TRUE(left);
}

TEST(Network, OpenRefusesAnUnsetProtocolId) {
  LoopbackNetwork loopback;
  Network net;
  EXPECT_FALSE(net.open(loopback.open(), TransportConfig())); // protocolId 0
  EXPECT_FALSE(net.isOpen());
  EXPECT_TRUE(net.open(loopback.open(), testConfig()));
}

// ---- A whole exchange over real UDP sockets ----

// Two peers on the real network stack, minus the board.
TEST(Network, PlaysAGameOfMessagesOverRealUdp) {
  Network white, black;
  ASSERT_TRUE(white.registerMessage<MoveMessage>());
  ASSERT_TRUE(black.registerMessage<MoveMessage>());
  ASSERT_TRUE(white.registerMessage<ChatMessage>(Channel::Unreliable));
  ASSERT_TRUE(black.registerMessage<ChatMessage>(Channel::Unreliable));
  ASSERT_TRUE(white.open(0, testConfig()));
  ASSERT_TRUE(black.open(0, testConfig()));
  white.setAcceptIncoming(true);

  std::vector<MoveMessage> whiteSaw, blackSaw;
  std::string chat;
  Address blackAddress;
  white.onMessage<MoveMessage>(
      [&](const Address &, const MoveMessage &m) { whiteSaw.push_back(m); });
  black.onMessage<MoveMessage>([&](const Address &from, const MoveMessage &m) {
    blackSaw.push_back(m);
    blackAddress = from;
    black.send(from, MoveMessage{52, 36, 0});
  });
  white.onMessage<ChatMessage>(
      [&](const Address &, const ChatMessage &m) { chat = m.text; });

  std::string target =
      "127.0.0.1:" + std::to_string(white.localAddress().port());
  ASSERT_TRUE(black.connect(target));
  bool sent = false;

  using Clock = std::chrono::steady_clock;
  auto start = Clock::now();
  auto seconds = [&] {
    return std::chrono::duration<double>(Clock::now() - start).count();
  };
  while (seconds() < 5.0 && (whiteSaw.empty() || chat.empty())) {
    white.update(seconds());
    black.update(seconds());
    if (!sent && !white.peers().empty()) {
      white.send(white.peers()[0], MoveMessage{12, 28, 0});
      sent = true;
    }
    if (!blackSaw.empty() && chat.empty()) {
      black.send(blackAddress, ChatMessage{"good game"});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  ASSERT_EQ(blackSaw.size(), 1u);
  EXPECT_EQ(blackSaw[0].from, 12);
  ASSERT_EQ(whiteSaw.size(), 1u);
  EXPECT_EQ(whiteSaw[0].from, 52);
  EXPECT_EQ(chat, "good game");
}
