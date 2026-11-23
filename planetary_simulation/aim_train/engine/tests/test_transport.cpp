#include "componeng/net/address.hpp"
#include "componeng/net/impaired_socket.hpp"
#include "componeng/net/loopback_socket.hpp"
#include "componeng/net/transport.hpp"
#include "gtest/gtest.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace componeng::net;

namespace {

using Bytes = std::vector<uint8_t>;

Bytes bytesOf(const std::string &text) {
  return Bytes(text.begin(), text.end());
}

Bytes counterPayload(uint32_t value, size_t extra = 0) {
  Bytes bytes(4 + extra);
  for (int i = 0; i < 4; ++i) {
    bytes[i] = static_cast<uint8_t>(value >> (8 * i));
  }
  for (size_t i = 0; i < extra; ++i) {
    bytes[4 + i] = static_cast<uint8_t>(value + i);
  }
  return bytes;
}

uint32_t counterOf(const Bytes &bytes) {
  uint32_t value = 0;
  for (int i = 0; i < 4 && i < static_cast<int>(bytes.size()); ++i) {
    value |= static_cast<uint32_t>(bytes[i]) << (8 * i);
  }
  return value;
}

// A loopback socket (optionally lossy), the Transport on it, and its events.
struct Peer {
  std::unique_ptr<LoopbackSocket> raw;
  std::unique_ptr<ImpairedSocket> impaired;
  std::unique_ptr<Transport> transport;
  std::vector<Event> events;
  Address address;
  bool paused = false;

  size_t count(Event::Type type) const {
    size_t n = 0;
    for (const Event &event : events) {
      n += event.type == type;
    }
    return n;
  }
  std::vector<Bytes> messages(Channel channel) const {
    std::vector<Bytes> result;
    for (const Event &event : events) {
      if (event.type == Event::Type::Message && event.channel == channel) {
        result.push_back(event.data);
      }
    }
    return result;
  }
  bool saw(Event::Type type, DisconnectReason reason) const {
    for (const Event &event : events) {
      if (event.type == type && event.reason == reason) {
        return true;
      }
    }
    return false;
  }
};

// Peers on one simulated network and clock: minutes of timeouts take no real
// time.
struct Net {
  LoopbackNetwork network;
  std::vector<std::unique_ptr<Peer>> peers;
  double now = 0.0;

  Peer &add(TransportConfig config = TransportConfig(),
            Impairment impairment = Impairment()) {
    auto peer = std::make_unique<Peer>();
    peer->raw = network.open();
    peer->address = peer->raw->localAddress();
    attach(*peer, config, impairment);
    peers.push_back(std::move(peer));
    return *peers.back();
  }

  void attach(Peer &peer, TransportConfig config, Impairment impairment) {
    // Distinct, repeatable connection ids; equal seeds would give equal salts.
    config.seed = static_cast<uint32_t>(peers.size() + 1) * 7919u;
    impairment.seed = static_cast<uint32_t>(peers.size() + 1);
    peer.impaired = std::make_unique<ImpairedSocket>(*peer.raw, impairment);
    peer.transport = std::make_unique<Transport>(*peer.impaired, config);
  }

  void step(double dt = 0.01) {
    now += dt;
    for (auto &peer : peers) {
      if (peer->paused) {
        continue;
      }
      peer->impaired->update(now);
      peer->transport->update(now);
      Event event;
      while (peer->transport->pollEvent(event)) {
        peer->events.push_back(std::move(event));
      }
    }
  }

  void run(double seconds, double dt = 0.01) {
    for (double end = now + seconds; now < end;) {
      step(dt);
    }
  }

  bool runUntil(const std::function<bool()> &done, double maxSeconds,
                double dt = 0.01) {
    for (double end = now + maxSeconds; now < end;) {
      if (done()) {
        return true;
      }
      step(dt);
    }
    return done();
  }

  void connect(Peer &from, Peer &to) {
    to.transport->setAcceptIncoming(true);
    ASSERT_TRUE(from.transport->connect(to.address));
    ASSERT_TRUE(runUntil(
        [&] {
          return from.transport->isConnected(to.address) &&
                 to.transport->isConnected(from.address);
        },
        10.0));
  }
};

Impairment badNetwork() {
  Impairment impairment;
  impairment.loss = 0.3;
  impairment.duplicate = 0.1;
  impairment.latency = 0.02;
  impairment.jitter = 0.05;
  return impairment;
}

} // namespace

// ---- ImpairedSocket ----

TEST(ImpairedSocket, PassesThroughWhenNotImpaired) {
  LoopbackNetwork network;
  auto a = network.open();
  auto b = network.open();
  ImpairedSocket impaired(*a, Impairment());
  ASSERT_EQ(impaired.sendTo(b->localAddress(), "x", 1), IoStatus::Ok);
  EXPECT_TRUE(b->waitReadable(0));
}

TEST(ImpairedSocket, LossOfOneDropsEverything) {
  LoopbackNetwork network;
  auto a = network.open();
  auto b = network.open();
  Impairment impairment;
  impairment.loss = 1.0;
  ImpairedSocket impaired(*a, impairment);
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(impaired.sendTo(b->localAddress(), "x", 1), IoStatus::Ok);
  }
  impaired.update(10.0);
  EXPECT_FALSE(b->waitReadable(0));
}

TEST(ImpairedSocket, LossRateIsRoughlyWhatWasAskedFor) {
  LoopbackNetwork network;
  auto a = network.open();
  auto b = network.open();
  Impairment impairment;
  impairment.loss = 0.5;
  ImpairedSocket impaired(*a, impairment);
  const int sent = 1000;
  for (int i = 0; i < sent; ++i) {
    impaired.sendTo(b->localAddress(), "x", 1);
  }
  int got = 0;
  char buffer[8];
  size_t received = 0;
  Address from;
  while (b->recvFrom(buffer, sizeof(buffer), received, from) == IoStatus::Ok) {
    ++got;
  }
  EXPECT_GT(got, 400);
  EXPECT_LT(got, 600);
}

TEST(ImpairedSocket, LatencyHoldsDatagramsBack) {
  LoopbackNetwork network;
  auto a = network.open();
  auto b = network.open();
  Impairment impairment;
  impairment.latency = 0.5;
  ImpairedSocket impaired(*a, impairment);
  impaired.update(1.0);
  impaired.sendTo(b->localAddress(), "x", 1);
  impaired.update(1.4);
  EXPECT_FALSE(b->waitReadable(0));
  impaired.update(1.5);
  EXPECT_TRUE(b->waitReadable(0));
}

TEST(ImpairedSocket, DuplicateOfOneSendsEverythingTwice) {
  LoopbackNetwork network;
  auto a = network.open();
  auto b = network.open();
  Impairment impairment;
  impairment.duplicate = 1.0;
  ImpairedSocket impaired(*a, impairment);
  impaired.sendTo(b->localAddress(), "x", 1);
  int got = 0;
  char buffer[8];
  size_t received = 0;
  Address from;
  while (b->recvFrom(buffer, sizeof(buffer), received, from) == IoStatus::Ok) {
    ++got;
  }
  EXPECT_EQ(got, 2);
}

TEST(ImpairedSocket, JitterReordersDatagrams) {
  LoopbackNetwork network;
  auto a = network.open();
  auto b = network.open();
  Impairment impairment;
  impairment.latency = 0.1;
  impairment.jitter = 1.0;
  ImpairedSocket impaired(*a, impairment);
  for (char c = 'a'; c < 'a' + 20; ++c) {
    impaired.sendTo(b->localAddress(), &c, 1);
  }
  impaired.update(10.0);
  std::string order;
  char buffer[8];
  size_t received = 0;
  Address from;
  while (b->recvFrom(buffer, sizeof(buffer), received, from) == IoStatus::Ok) {
    order += buffer[0];
  }
  EXPECT_EQ(order.size(), 20u);
  EXPECT_NE(order, "abcdefghijklmnopqrst");
}

// ---- Handshake ----

TEST(Transport, ConnectsAndReportsBothSides) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);

  EXPECT_EQ(a.count(Event::Type::Connected), 1u);
  EXPECT_EQ(b.count(Event::Type::Connected), 1u);
  EXPECT_EQ(a.events[0].peer, b.address);
  EXPECT_EQ(b.events[0].peer, a.address);
  EXPECT_EQ(a.transport->connectedPeers(), std::vector<Address>{b.address});
}

TEST(Transport, ConnectFailsForInvalidDuplicateOrOverLimit) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  EXPECT_FALSE(a.transport->connect(Address()));
  EXPECT_TRUE(a.transport->connect(b.address));
  EXPECT_FALSE(a.transport->connect(b.address));

  TransportConfig tiny;
  tiny.maxConnections = 1;
  Peer &c = net.add(tiny);
  EXPECT_TRUE(c.transport->connect(a.address));
  EXPECT_FALSE(c.transport->connect(b.address));
}

TEST(Transport, RefusedWhenNotAccepting) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  ASSERT_TRUE(a.transport->connect(b.address));
  net.run(1.0);
  EXPECT_TRUE(a.saw(Event::Type::ConnectFailed, DisconnectReason::Rejected));
  EXPECT_EQ(a.count(Event::Type::Connected), 0u);
  EXPECT_TRUE(b.transport->connectedPeers().empty());
}

TEST(Transport, RefusedWhenFull) {
  Net net;
  TransportConfig one;
  one.maxConnections = 1;
  Peer &host = net.add(one);
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, host);
  ASSERT_TRUE(b.transport->connect(host.address));
  net.run(1.0);
  EXPECT_TRUE(b.saw(Event::Type::ConnectFailed, DisconnectReason::Rejected));
  EXPECT_TRUE(host.transport->isConnected(a.address));
}

TEST(Transport, ConnectTimesOutWhenNobodyAnswers) {
  Net net;
  Peer &a = net.add();
  Peer &gone = net.add();
  Address nobody = gone.address;
  gone.raw->close();
  ASSERT_TRUE(a.transport->connect(nobody));
  net.run(4.0);
  EXPECT_EQ(a.count(Event::Type::ConnectFailed), 0u);
  net.run(2.0);
  EXPECT_TRUE(
      a.saw(Event::Type::ConnectFailed, DisconnectReason::ConnectTimeout));
}

TEST(Transport, StraysWithAnotherProtocolIdAreIgnored) {
  Net net;
  TransportConfig other;
  other.protocolId = 42;
  Peer &a = net.add();
  Peer &b = net.add(other);
  b.transport->setAcceptIncoming(true);
  ASSERT_TRUE(a.transport->connect(b.address));
  net.run(6.0);
  EXPECT_TRUE(
      a.saw(Event::Type::ConnectFailed, DisconnectReason::ConnectTimeout));
  EXPECT_TRUE(b.events.empty());
}

TEST(Transport, ConnectsDespiteLossAndReordering) {
  Net net;
  Peer &a = net.add(TransportConfig(), badNetwork());
  Peer &b = net.add(TransportConfig(), badNetwork());
  net.connect(a, b);
  EXPECT_EQ(a.count(Event::Type::Connected), 1u);
  EXPECT_EQ(b.count(Event::Type::Connected), 1u);
}

TEST(Transport, SimultaneousConnectMakesOneConnection) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  // Neither accepts: each only started its own request, as in a simultaneous
  // punch.
  ASSERT_TRUE(a.transport->connect(b.address));
  ASSERT_TRUE(b.transport->connect(a.address));
  ASSERT_TRUE(net.runUntil(
      [&] {
        return a.transport->isConnected(b.address) &&
               b.transport->isConnected(a.address);
      },
      5.0));
  net.run(1.0);
  EXPECT_EQ(a.count(Event::Type::Connected), 1u);
  EXPECT_EQ(b.count(Event::Type::Connected), 1u);

  ASSERT_TRUE(a.transport->send(b.address, Channel::Reliable, "hi", 2));
  ASSERT_TRUE(net.runUntil(
      [&] { return !b.messages(Channel::Reliable).empty(); }, 2.0));
}

TEST(Transport, ManyPeersConnectToOne) {
  Net net;
  Peer &host = net.add();
  std::vector<Peer *> guests;
  for (int i = 0; i < 4; ++i) {
    guests.push_back(&net.add());
  }
  host.transport->setAcceptIncoming(true);
  for (Peer *guest : guests) {
    ASSERT_TRUE(guest->transport->connect(host.address));
  }
  ASSERT_TRUE(net.runUntil(
      [&] { return host.transport->connectedPeers().size() == 4; }, 5.0));

  for (size_t i = 0; i < guests.size(); ++i) {
    Bytes payload = counterPayload(static_cast<uint32_t>(i));
    ASSERT_TRUE(host.transport->send(guests[i]->address, Channel::Reliable,
                                     payload.data(), payload.size()));
  }
  net.run(1.0);
  for (size_t i = 0; i < guests.size(); ++i) {
    auto messages = guests[i]->messages(Channel::Reliable);
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(counterOf(messages[0]), i);
  }
}

TEST(Transport, IgnoresGarbageAndForgedPackets) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);
  size_t eventsBefore = b.events.size();

  auto stray = net.network.open();
  std::vector<Bytes> junk = {
      {},
      {1},
      bytesOf("not a packet at all, just text"),
      Bytes(kMaxPacketSize, 0xFF),
      Bytes(kMaxPacketSize, 0x00),
  };
  for (const Bytes &packet : junk) {
    stray->sendTo(b.address, packet.data(), packet.size());
    stray->sendTo(a.address, packet.data(), packet.size());
  }
  net.run(0.5);
  EXPECT_EQ(b.events.size(), eventsBefore);
  EXPECT_TRUE(b.transport->isConnected(a.address));
  EXPECT_EQ(b.transport->connectedPeers().size(), 1u);
}

// ---- Messages ----

TEST(Transport, ReliableAndUnreliableMessagesArrive) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);

  ASSERT_TRUE(a.transport->send(b.address, Channel::Reliable, "move e2e4", 9));
  ASSERT_TRUE(a.transport->send(b.address, Channel::Unreliable, "cursor", 6));
  net.run(0.5);

  auto reliable = b.messages(Channel::Reliable);
  auto unreliable = b.messages(Channel::Unreliable);
  ASSERT_EQ(reliable.size(), 1u);
  ASSERT_EQ(unreliable.size(), 1u);
  EXPECT_EQ(reliable[0], bytesOf("move e2e4"));
  EXPECT_EQ(unreliable[0], bytesOf("cursor"));
  for (const Event &event : b.events) {
    if (event.type == Event::Type::Message) {
      EXPECT_EQ(event.peer, a.address);
    }
  }
}

TEST(Transport, EmptyMessagesAreMessages) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);
  ASSERT_TRUE(a.transport->send(b.address, Channel::Reliable, nullptr, 0));
  ASSERT_TRUE(a.transport->send(b.address, Channel::Unreliable, nullptr, 0));
  net.run(0.5);
  ASSERT_EQ(b.messages(Channel::Reliable).size(), 1u);
  EXPECT_TRUE(b.messages(Channel::Reliable)[0].empty());
  ASSERT_EQ(b.messages(Channel::Unreliable).size(), 1u);
}

TEST(Transport, SendIsRefusedWithoutAConnectionOrForBadSizes) {
  Net net;
  TransportConfig config;
  config.maxMessageSize = 5000;
  Peer &a = net.add(config);
  Peer &b = net.add();
  char x = 'x';
  EXPECT_FALSE(a.transport->send(b.address, Channel::Reliable, &x, 1));

  net.connect(a, b);
  Bytes unreliableMax(kMaxUnreliableMessageSize);
  Bytes unreliableTooBig(kMaxUnreliableMessageSize + 1);
  EXPECT_TRUE(a.transport->send(b.address, Channel::Unreliable,
                                unreliableMax.data(), unreliableMax.size()));
  EXPECT_FALSE(a.transport->send(b.address, Channel::Unreliable,
                                 unreliableTooBig.data(),
                                 unreliableTooBig.size()));
  Bytes reliableMax(5000);
  Bytes reliableTooBig(5001);
  EXPECT_TRUE(a.transport->send(b.address, Channel::Reliable,
                                reliableMax.data(), reliableMax.size()));
  EXPECT_FALSE(a.transport->send(b.address, Channel::Reliable,
                                 reliableTooBig.data(), reliableTooBig.size()));

  EXPECT_FALSE(
      a.transport->send(Address::loopback(9), Channel::Reliable, &x, 1));
}

TEST(Transport, LargeReliableMessageIsSplitAndReassembled) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);

  Bytes big(300000);
  for (size_t i = 0; i < big.size(); ++i) {
    big[i] = static_cast<uint8_t>(i * 31 + (i >> 8));
  }
  ASSERT_TRUE(
      a.transport->send(b.address, Channel::Reliable, big.data(), big.size()));
  ASSERT_TRUE(net.runUntil(
      [&] { return !b.messages(Channel::Reliable).empty(); }, 30.0));
  auto messages = b.messages(Channel::Reliable);
  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(messages[0], big);
}

TEST(Transport, LargeMessageSurvivesALossyNetwork) {
  Net net;
  Impairment lossy = badNetwork();
  Peer &a = net.add(TransportConfig(), lossy);
  Peer &b = net.add(TransportConfig(), lossy);
  net.connect(a, b);

  Bytes big(100000);
  for (size_t i = 0; i < big.size(); ++i) {
    big[i] = static_cast<uint8_t>(i * 13);
  }
  ASSERT_TRUE(
      a.transport->send(b.address, Channel::Reliable, big.data(), big.size()));
  ASSERT_TRUE(net.runUntil(
      [&] { return !b.messages(Channel::Reliable).empty(); }, 120.0));
  EXPECT_EQ(b.messages(Channel::Reliable)[0], big);
  EXPECT_GT(a.transport->stats(b.address).reliableResends, 0u);
}

TEST(Transport, ReliableMessagesArriveOnceAndInOrderOnABadNetwork) {
  Net net;
  Peer &a = net.add(TransportConfig(), badNetwork());
  Peer &b = net.add(TransportConfig(), badNetwork());
  net.connect(a, b);

  const uint32_t total = 400;
  uint32_t next = 0;
  ASSERT_TRUE(net.runUntil(
      [&] {
        for (int burst = 0; burst < 3 && next < total; ++burst, ++next) {
          Bytes payload = counterPayload(next, (next * 37) % 3000);
          EXPECT_TRUE(a.transport->send(b.address, Channel::Reliable,
                                        payload.data(), payload.size()));
          Bytes reply = counterPayload(next, (next * 11) % 200);
          EXPECT_TRUE(b.transport->send(a.address, Channel::Reliable,
                                        reply.data(), reply.size()));
        }
        return next == total && b.messages(Channel::Reliable).size() == total &&
               a.messages(Channel::Reliable).size() == total;
      },
      300.0));

  auto atB = b.messages(Channel::Reliable);
  auto atA = a.messages(Channel::Reliable);
  ASSERT_EQ(atB.size(), total);
  ASSERT_EQ(atA.size(), total);
  for (uint32_t i = 0; i < total; ++i) {
    ASSERT_EQ(atB[i], counterPayload(i, (i * 37) % 3000)) << "message " << i;
    ASSERT_EQ(atA[i], counterPayload(i, (i * 11) % 200)) << "message " << i;
  }
  EXPECT_TRUE(a.transport->isConnected(b.address));
}

TEST(Transport, DuplicatedPacketsDoNotDuplicateMessages) {
  Net net;
  Impairment doubling;
  doubling.duplicate = 1.0;
  Peer &a = net.add(TransportConfig(), doubling);
  Peer &b = net.add();
  net.connect(a, b);
  for (uint32_t i = 0; i < 50; ++i) {
    Bytes payload = counterPayload(i);
    a.transport->send(b.address, Channel::Unreliable, payload.data(),
                      payload.size());
    a.transport->send(b.address, Channel::Reliable, payload.data(),
                      payload.size());
    net.step();
  }
  net.run(1.0);
  EXPECT_EQ(b.messages(Channel::Unreliable).size(), 50u);
  EXPECT_EQ(b.messages(Channel::Reliable).size(), 50u);
}

TEST(Transport, UnreliableMessagesAreSimplyLostOnALossyNetwork) {
  Net net;
  Impairment lossy;
  lossy.loss = 0.5;
  Peer &a = net.add(TransportConfig(), lossy);
  Peer &b = net.add();
  net.connect(a, b);
  const int total = 400;
  for (int i = 0; i < total; ++i) {
    Bytes payload = counterPayload(static_cast<uint32_t>(i));
    a.transport->send(b.address, Channel::Unreliable, payload.data(),
                      payload.size());
    net.step();
  }
  net.run(1.0);
  size_t got = b.messages(Channel::Unreliable).size();
  EXPECT_GT(got, 100u);
  EXPECT_LT(got, 300u);
}

TEST(Transport, SendFailsWhenTheQueueIsFullAndRecoversAfter) {
  Net net;
  TransportConfig config;
  config.maxQueuedBytes = 10000;
  Peer &a = net.add(config);
  Peer &b = net.add();
  net.connect(a, b);

  Bytes chunk(6000);
  EXPECT_TRUE(a.transport->send(b.address, Channel::Reliable, chunk.data(),
                                chunk.size()));
  EXPECT_FALSE(a.transport->send(b.address, Channel::Reliable, chunk.data(),
                                 chunk.size()));
  ASSERT_TRUE(net.runUntil(
      [&] { return b.messages(Channel::Reliable).size() == 1; }, 5.0));
  net.run(0.5);
  EXPECT_TRUE(a.transport->send(b.address, Channel::Reliable, chunk.data(),
                                chunk.size()));
}

TEST(Transport, RoundTripTimeIsMeasured) {
  Net net;
  Impairment slow;
  slow.latency = 0.05;
  Peer &a = net.add(TransportConfig(), slow);
  Peer &b = net.add(TransportConfig(), slow);
  net.connect(a, b);
  for (int i = 0; i < 100; ++i) {
    a.transport->send(b.address, Channel::Unreliable, "p", 1);
    net.run(0.05, 0.005);
  }
  double rtt = a.transport->stats(b.address).rtt;
  EXPECT_GT(rtt, 0.09);
  EXPECT_LT(rtt, 0.16);
  EXPECT_GT(a.transport->stats(b.address).packetsSent, 0u);
  EXPECT_GT(b.transport->stats(a.address).bytesReceived, 0u);
  EXPECT_EQ(a.transport->stats(Address::loopback(9)).packetsSent, 0u);
}

// ---- Lifetime of a connection ----

TEST(Transport, IdleConnectionsStayAlive) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);
  net.run(30.0, 0.05);
  EXPECT_TRUE(a.transport->isConnected(b.address));
  EXPECT_TRUE(b.transport->isConnected(a.address));
  EXPECT_EQ(a.events.size(), 1u);
  EXPECT_EQ(b.events.size(), 1u);
}

TEST(Transport, SilentPeerTimesOut) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);
  b.paused = true;
  net.run(9.0);
  EXPECT_TRUE(a.transport->isConnected(b.address));
  net.run(2.0);
  EXPECT_FALSE(a.transport->isConnected(b.address));
  EXPECT_TRUE(a.saw(Event::Type::Disconnected, DisconnectReason::Timeout));
  EXPECT_FALSE(a.transport->send(b.address, Channel::Reliable, "x", 1));
}

TEST(Transport, DisconnectIsReportedToBothSides) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);
  a.transport->disconnect(b.address);
  net.run(0.2);
  EXPECT_TRUE(a.saw(Event::Type::Disconnected, DisconnectReason::Local));
  EXPECT_TRUE(b.saw(Event::Type::Disconnected, DisconnectReason::Remote));
  EXPECT_FALSE(a.transport->isConnected(b.address));
  EXPECT_FALSE(b.transport->isConnected(a.address));
}

TEST(Transport, DestroyingATransportDisconnectsItsPeers) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);
  a.transport.reset();
  a.paused = true;
  net.run(0.2);
  EXPECT_TRUE(b.saw(Event::Type::Disconnected, DisconnectReason::Remote));
}

TEST(Transport, ReconnectingAfterADisconnectWorks) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);
  a.transport->disconnect(b.address);
  net.run(0.2);
  net.connect(a, b);
  ASSERT_TRUE(a.transport->send(b.address, Channel::Reliable, "again", 5));
  ASSERT_TRUE(net.runUntil(
      [&] { return !b.messages(Channel::Reliable).empty(); }, 2.0));
  EXPECT_EQ(b.messages(Channel::Reliable)[0], bytesOf("again"));
}

TEST(Transport, AbandoningAHandshakeReportsConnectFailed) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  b.paused = true;
  ASSERT_TRUE(a.transport->connect(b.address));
  net.run(0.5);
  a.transport->disconnect(b.address);
  net.step();
  EXPECT_TRUE(a.saw(Event::Type::ConnectFailed, DisconnectReason::Local));
  EXPECT_TRUE(a.transport->connect(b.address));
}

TEST(Transport, PeerThatRestartsReplacesItsOldConnection) {
  Net net;
  Peer &a = net.add();
  Peer &b = net.add();
  net.connect(a, b);
  a.transport->setAcceptIncoming(true);
  Address address = b.address;

  // B "crashes": its socket goes away without a goodbye, and a new process
  // takes the same port.
  b.raw->close();
  b.transport.reset();
  b.impaired.reset();
  b.raw = net.network.open(address.port());
  ASSERT_TRUE(b.raw);
  b.events.clear();
  net.attach(b, TransportConfig(), Impairment());
  b.transport->setAcceptIncoming(true);
  ASSERT_TRUE(b.transport->connect(a.address));

  ASSERT_TRUE(
      net.runUntil([&] { return b.transport->isConnected(a.address); }, 5.0));
  net.run(0.5);
  EXPECT_TRUE(a.saw(Event::Type::Disconnected, DisconnectReason::Remote));
  EXPECT_EQ(a.count(Event::Type::Connected), 2u);
  EXPECT_TRUE(a.transport->isConnected(b.address));

  ASSERT_TRUE(
      a.transport->send(b.address, Channel::Reliable, "hello again", 11));
  ASSERT_TRUE(net.runUntil(
      [&] { return !b.messages(Channel::Reliable).empty(); }, 2.0));
}

// Packet sequence numbers and reliable ids are 16 bits and wrap at 65536; drive
// both well past it on a bad network.
TEST(Transport, SequenceNumbersAndMessageIdsSurviveWrappingAround) {
  Net net;
  Impairment rough;
  rough.loss = 0.05;
  rough.duplicate = 0.05;
  rough.latency = 0.005;
  rough.jitter = 0.02;
  Peer &a = net.add(TransportConfig(), rough);
  Peer &b = net.add(TransportConfig(), rough);
  net.connect(a, b);

  const uint32_t total = 80000; // more than 65536 reliable messages
  uint32_t sent = 0;
  Bytes filler(1100);
  uint64_t unreliableSent = 0;
  bool ok = net.runUntil(
      [&] {
        // A reliable burst plus enough unreliable payload for about a dozen
        // packets per step, so the packet sequence wraps too.
        for (int i = 0; i < 10 && sent < total; ++i, ++sent) {
          Bytes payload = counterPayload(sent);
          EXPECT_TRUE(a.transport->send(b.address, Channel::Reliable,
                                        payload.data(), payload.size()));
        }
        for (int i = 0; i < 12; ++i) {
          if (a.transport->send(b.address, Channel::Unreliable, filler.data(),
                                filler.size())) {
            ++unreliableSent;
          }
        }
        return sent == total && b.messages(Channel::Reliable).size() == total;
      },
      600.0);
  ASSERT_TRUE(ok) << "received " << b.messages(Channel::Reliable).size()
                  << " of " << total;

  auto received = b.messages(Channel::Reliable);
  ASSERT_EQ(received.size(), total);
  for (uint32_t i = 0; i < total; ++i) {
    ASSERT_EQ(counterOf(received[i]), i) << "message " << i;
  }
  EXPECT_GT(a.transport->stats(b.address).packetsSent, 65536u);
  EXPECT_TRUE(a.transport->isConnected(b.address));
  EXPECT_TRUE(b.transport->isConnected(a.address));
  EXPECT_GT(unreliableSent, 0u);
}
