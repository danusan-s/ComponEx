#include "componeng/net/address.hpp"
#include "componeng/net/idatagram_socket.hpp"
#include "componeng/net/loopback_socket.hpp"
#include "componeng/net/udp_socket.hpp"
#include "gtest/gtest.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace componeng::net;

namespace {

IoStatus sendString(IDatagramSocket &socket, const Address &to,
                    const std::string &text) {
  return socket.sendTo(to, text.data(), text.size());
}

std::string receiveString(IDatagramSocket &socket, Address &from, bool &ok) {
  char buffer[kMaxPacketSize];
  size_t received = 0;
  ok = socket.recvFrom(buffer, sizeof(buffer), received, from) == IoStatus::Ok;
  return ok ? std::string(buffer, received) : std::string();
}

// Behaviour both sockets must share, so the loopback stays a faithful
// stand-in for the real one.
void checkExchange(IDatagramSocket &a, IDatagramSocket &b,
                   const Address &addressOfA, const Address &addressOfB) {
  char buffer[kMaxPacketSize];
  size_t received = 0;
  Address from;

  EXPECT_FALSE(b.waitReadable(0));
  EXPECT_EQ(b.recvFrom(buffer, sizeof(buffer), received, from),
            IoStatus::WouldBlock);

  ASSERT_EQ(sendString(a, addressOfB, "hello"), IoStatus::Ok);
  ASSERT_TRUE(b.waitReadable(1000));
  bool ok = false;
  EXPECT_EQ(receiveString(b, from, ok), "hello");
  EXPECT_TRUE(ok);
  EXPECT_EQ(from.port(), addressOfA.port());
  EXPECT_EQ(from, addressOfA);

  ASSERT_EQ(sendString(b, addressOfA, "reply"), IoStatus::Ok);
  ASSERT_TRUE(a.waitReadable(1000));
  EXPECT_EQ(receiveString(a, from, ok), "reply");
  EXPECT_EQ(from, addressOfB);

  ASSERT_EQ(a.sendTo(addressOfB, nullptr, 0), IoStatus::Ok);
  ASSERT_TRUE(b.waitReadable(1000));
  ASSERT_EQ(b.recvFrom(buffer, sizeof(buffer), received, from), IoStatus::Ok);
  EXPECT_EQ(received, 0u);

  std::vector<uint8_t> big(kMaxPacketSize);
  for (size_t i = 0; i < big.size(); ++i) {
    big[i] = static_cast<uint8_t>(i * 7);
  }
  ASSERT_EQ(a.sendTo(addressOfB, big.data(), big.size()), IoStatus::Ok);
  ASSERT_TRUE(b.waitReadable(1000));
  ASSERT_EQ(b.recvFrom(buffer, sizeof(buffer), received, from), IoStatus::Ok);
  ASSERT_EQ(received, big.size());
  EXPECT_EQ(std::memcmp(buffer, big.data(), received), 0);

  // Over the limit is refused, not fragmented.
  std::vector<uint8_t> tooBig(kMaxPacketSize + 1);
  EXPECT_EQ(a.sendTo(addressOfB, tooBig.data(), tooBig.size()),
            IoStatus::Error);
  EXPECT_FALSE(a.lastError().empty());

  // An oversized datagram is discarded whole; the next one still arrives.
  ASSERT_EQ(sendString(a, addressOfB, "this one is too long"), IoStatus::Ok);
  ASSERT_EQ(sendString(a, addressOfB, "ok"), IoStatus::Ok);
  ASSERT_TRUE(b.waitReadable(1000));
  char small[4];
  ASSERT_EQ(b.recvFrom(small, sizeof(small), received, from), IoStatus::Ok);
  EXPECT_EQ(std::string(small, received), "ok");

  EXPECT_EQ(a.sendTo(Address::loopback(1, Address::Family::IPv6), "x", 1),
            IoStatus::Error);
}

} // namespace

// ---- Address ----

TEST(Address, DefaultIsInvalid) {
  Address address;
  EXPECT_FALSE(address.isValid());
  EXPECT_EQ(address, Address());
  EXPECT_EQ(address.toString(), "(invalid)");
}

TEST(Address, ParsesIPv4) {
  Address address;
  ASSERT_TRUE(Address::parse("192.168.1.20:5000", address));
  EXPECT_EQ(address.family(), Address::Family::IPv4);
  EXPECT_EQ(address.port(), 5000);
  EXPECT_EQ(address, Address::ipv4(192, 168, 1, 20, 5000));
  EXPECT_EQ(address.toString(), "192.168.1.20:5000");
}

TEST(Address, ParsesIPv6) {
  Address address;
  ASSERT_TRUE(Address::parse("[::1]:6000", address));
  EXPECT_EQ(address.family(), Address::Family::IPv6);
  EXPECT_EQ(address, Address::loopback(6000, Address::Family::IPv6));
  EXPECT_EQ(address.toString(), "[::1]:6000");
}

TEST(Address, ToStringRoundTrips) {
  for (const char *text : {"10.0.0.1:1", "0.0.0.0:0", "255.255.255.255:65535",
                           "[2001:db8::ff00:42:8329]:80", "[::]:0"}) {
    Address address;
    ASSERT_TRUE(Address::parse(text, address)) << text;
    EXPECT_EQ(address.toString(), text);
  }
}

TEST(Address, RejectsMalformedText) {
  for (const char *text :
       {"", "1.2.3.4", "1.2.3.4:", ":5000", "1.2.3.4:65536", "1.2.3.4:-1",
        "1.2.3.4:50a", "1.2.3.4:5000x", "1.2.3:5000", "256.1.1.1:5000",
        "localhost:5000", "::1:5000", "[::1]", "[::1]5000", "[::1:5000",
        "[1.2.3.4]:5000", "[::1]:", " 1.2.3.4:5000"}) {
    Address address = Address::ipv4(9, 9, 9, 9, 9);
    EXPECT_FALSE(Address::parse(text, address)) << "'" << text << "'";
    // A failed parse leaves the output alone.
    EXPECT_EQ(address, Address::ipv4(9, 9, 9, 9, 9));
  }
}

TEST(Address, EqualityComparesFamilyPortAndBytes) {
  EXPECT_EQ(Address::ipv4(1, 2, 3, 4, 5), Address::ipv4(1, 2, 3, 4, 5));
  EXPECT_NE(Address::ipv4(1, 2, 3, 4, 5), Address::ipv4(1, 2, 3, 4, 6));
  EXPECT_NE(Address::ipv4(1, 2, 3, 4, 5), Address::ipv4(1, 2, 3, 5, 5));
  EXPECT_NE(Address::any(5, Address::Family::IPv4),
            Address::any(5, Address::Family::IPv6));
}

TEST(Address, WorksAsHashKey) {
  std::unordered_set<Address> set;
  set.insert(Address::ipv4(1, 2, 3, 4, 5));
  set.insert(Address::ipv4(1, 2, 3, 4, 5));
  set.insert(Address::ipv4(1, 2, 3, 4, 6));
  set.insert(Address::loopback(5, Address::Family::IPv6));
  EXPECT_EQ(set.size(), 3u);
  EXPECT_EQ(set.count(Address::ipv4(1, 2, 3, 4, 6)), 1u);
}

// ---- UdpSocket ----

TEST(UdpSocket, OpenReportsTheBoundPort) {
  UdpSocket socket;
  EXPECT_FALSE(socket.isOpen());
  ASSERT_TRUE(socket.open(Address::loopback(0)));
  EXPECT_TRUE(socket.isOpen());
  EXPECT_NE(socket.localAddress().port(), 0);
  socket.close();
  EXPECT_FALSE(socket.isOpen());
  EXPECT_FALSE(socket.localAddress().isValid());
}

TEST(UdpSocket, ExchangesDatagrams) {
  UdpSocket a, b;
  ASSERT_TRUE(a.open(Address::loopback(0)));
  ASSERT_TRUE(b.open(Address::loopback(0)));
  checkExchange(a, b, a.localAddress(), b.localAddress());
}

TEST(UdpSocket, ExchangesDatagramsOverIPv6) {
  UdpSocket a, b;
  if (!a.open(Address::loopback(0, Address::Family::IPv6)) ||
      !b.open(Address::loopback(0, Address::Family::IPv6))) {
    GTEST_SKIP() << "IPv6 loopback is not available";
  }
  ASSERT_EQ(sendString(a, b.localAddress(), "v6"), IoStatus::Ok);
  ASSERT_TRUE(b.waitReadable(1000));
  Address from;
  bool ok = false;
  EXPECT_EQ(receiveString(b, from, ok), "v6");
  EXPECT_EQ(from, a.localAddress());
}

TEST(UdpSocket, ReceivesOnTheWildcardAddress) {
  UdpSocket server, client;
  ASSERT_TRUE(server.open(Address::any(0)));
  ASSERT_TRUE(client.open(Address::loopback(0)));
  Address target = Address::loopback(server.localAddress().port());
  ASSERT_EQ(sendString(client, target, "hi"), IoStatus::Ok);
  ASSERT_TRUE(server.waitReadable(1000));
  Address from;
  bool ok = false;
  EXPECT_EQ(receiveString(server, from, ok), "hi");
  EXPECT_EQ(from, client.localAddress());
}

TEST(UdpSocket, WaitReadableTimesOutWhenIdle) {
  UdpSocket socket;
  ASSERT_TRUE(socket.open(Address::loopback(0)));
  EXPECT_FALSE(socket.waitReadable(0));
  EXPECT_FALSE(socket.waitReadable(10));
}

TEST(UdpSocket, SecondBindOnTheSamePortFails) {
  UdpSocket first, second;
  ASSERT_TRUE(first.open(Address::loopback(0)));
  EXPECT_FALSE(second.open(first.localAddress()));
  EXPECT_FALSE(second.isOpen());
  EXPECT_FALSE(second.lastError().empty());
  Address taken = first.localAddress();
  first.close();
  EXPECT_TRUE(second.open(taken));
}

TEST(UdpSocket, ReopeningClosesTheOldSocket) {
  UdpSocket socket;
  ASSERT_TRUE(socket.open(Address::loopback(0)));
  Address first = socket.localAddress();
  ASSERT_TRUE(socket.open(Address::loopback(0)));
  UdpSocket probe;
  EXPECT_TRUE(probe.open(first));
}

TEST(UdpSocket, RejectsAnInvalidAddress) {
  UdpSocket socket;
  EXPECT_FALSE(socket.open(Address()));
  EXPECT_FALSE(socket.isOpen());
}

TEST(UdpSocket, ClosedSocketReportsErrors) {
  UdpSocket socket;
  size_t received = 0;
  Address from;
  char buffer[8];
  EXPECT_EQ(socket.sendTo(Address::loopback(1), "x", 1), IoStatus::Error);
  EXPECT_EQ(socket.recvFrom(buffer, sizeof(buffer), received, from),
            IoStatus::Error);
  EXPECT_FALSE(socket.waitReadable(0));
}

TEST(UdpSocket, SendingToNobodyIsNotAnError) {
  UdpSocket a, gone;
  ASSERT_TRUE(a.open(Address::loopback(0)));
  ASSERT_TRUE(gone.open(Address::loopback(0)));
  Address nobody = gone.localAddress();
  gone.close();
  EXPECT_EQ(sendString(a, nobody, "into the void"), IoStatus::Ok);
  // A stray port-unreachable must not break receiving.
  UdpSocket b;
  ASSERT_TRUE(b.open(Address::loopback(0)));
  ASSERT_EQ(sendString(b, a.localAddress(), "still works"), IoStatus::Ok);
  ASSERT_TRUE(a.waitReadable(1000));
  Address from;
  bool ok = false;
  EXPECT_EQ(receiveString(a, from, ok), "still works");
}

TEST(UdpSocket, MoveTransfersOwnership) {
  UdpSocket original;
  ASSERT_TRUE(original.open(Address::loopback(0)));
  Address address = original.localAddress();

  UdpSocket moved(std::move(original));
  EXPECT_FALSE(original.isOpen());
  EXPECT_TRUE(moved.isOpen());
  EXPECT_EQ(moved.localAddress(), address);

  UdpSocket assigned;
  assigned = std::move(moved);
  EXPECT_FALSE(moved.isOpen());
  EXPECT_EQ(assigned.localAddress(), address);

  UdpSocket peer;
  ASSERT_TRUE(peer.open(Address::loopback(0)));
  ASSERT_EQ(sendString(peer, address, "ping"), IoStatus::Ok);
  ASSERT_TRUE(assigned.waitReadable(1000));
}

// ---- LoopbackSocket ----

TEST(LoopbackSocket, ExchangesDatagrams) {
  LoopbackNetwork network;
  auto a = network.open();
  auto b = network.open();
  ASSERT_TRUE(a && b);
  checkExchange(*a, *b, a->localAddress(), b->localAddress());
}

TEST(LoopbackSocket, EphemeralPortsAreDistinct) {
  LoopbackNetwork network;
  auto a = network.open();
  auto b = network.open();
  ASSERT_TRUE(a && b);
  EXPECT_NE(a->localAddress().port(), b->localAddress().port());
  EXPECT_NE(a->localAddress().port(), 0);
}

TEST(LoopbackSocket, ExplicitPortIsExclusiveUntilClosed) {
  LoopbackNetwork network;
  auto a = network.open(4000);
  ASSERT_TRUE(a);
  EXPECT_EQ(a->localAddress(), Address::loopback(4000));
  EXPECT_EQ(network.open(4000), nullptr);
  a->close();
  EXPECT_NE(network.open(4000), nullptr);
}

TEST(LoopbackSocket, DatagramsKeepTheirOrder) {
  LoopbackNetwork network;
  auto a = network.open();
  auto b = network.open();
  Address from;
  bool ok = false;
  for (int i = 0; i < 5; ++i) {
    ASSERT_EQ(sendString(*a, b->localAddress(), std::to_string(i)),
              IoStatus::Ok);
  }
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(receiveString(*b, from, ok), std::to_string(i));
  }
  receiveString(*b, from, ok);
  EXPECT_FALSE(ok);
}

TEST(LoopbackSocket, NetworksAreIsolated) {
  LoopbackNetwork one, two;
  auto a = one.open(4000);
  auto b = two.open(4001);
  ASSERT_EQ(sendString(*a, b->localAddress(), "lost"), IoStatus::Ok);
  EXPECT_FALSE(b->waitReadable(0));
}

TEST(LoopbackSocket, SendingToNobodyIsNotAnError) {
  LoopbackNetwork network;
  auto a = network.open();
  EXPECT_EQ(sendString(*a, Address::loopback(9), "x"), IoStatus::Ok);
  EXPECT_EQ(sendString(*a, Address::ipv4(10, 0, 0, 1, 9), "x"), IoStatus::Ok);
}

TEST(LoopbackSocket, FullQueueDropsNewArrivals) {
  LoopbackNetwork network;
  auto a = network.open();
  auto b = network.open();
  for (size_t i = 0; i < LoopbackSocket::kMaxQueued + 10; ++i) {
    ASSERT_EQ(sendString(*a, b->localAddress(), "x"), IoStatus::Ok);
  }
  size_t count = 0;
  char buffer[8];
  size_t received = 0;
  Address from;
  while (b->recvFrom(buffer, sizeof(buffer), received, from) == IoStatus::Ok) {
    ++count;
  }
  EXPECT_EQ(count, LoopbackSocket::kMaxQueued);
}

TEST(LoopbackSocket, ClosedSocketReportsErrors) {
  LoopbackNetwork network;
  auto a = network.open();
  a->close();
  EXPECT_FALSE(a->isOpen());
  size_t received = 0;
  Address from;
  char buffer[8];
  EXPECT_EQ(a->sendTo(Address::loopback(1), "x", 1), IoStatus::Error);
  EXPECT_EQ(a->recvFrom(buffer, sizeof(buffer), received, from),
            IoStatus::Error);
}

TEST(LoopbackSocket, OutlivingTheNetworkIsSafe) {
  std::unique_ptr<LoopbackSocket> socket;
  {
    LoopbackNetwork network;
    socket = network.open(4000);
    ASSERT_TRUE(socket);
  }
  EXPECT_FALSE(socket->isOpen());
  EXPECT_EQ(socket->sendTo(Address::loopback(4000), "x", 1), IoStatus::Error);
  socket.reset();
}
