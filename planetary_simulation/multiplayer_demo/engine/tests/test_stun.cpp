#include "componeng/net/address.hpp"
#include "componeng/net/loopback_socket.hpp"
#include "componeng/net/nat/invite_code.hpp"
#include "componeng/net/nat/nat_discovery.hpp"
#include "componeng/net/nat/nat_traversal.hpp"
#include "componeng/net/nat/stun.hpp"
#include "componeng/net/nat/stun_socket.hpp"
#include "componeng/net/network.hpp"
#include "componeng/net/transport.hpp"
#include "nat_sim.hpp"
#include "net_test_config.hpp"
#include "gtest/gtest.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace componeng::net;
using Bytes = std::vector<uint8_t>;

namespace {

const stun::TransactionId kRfcId = {0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34,
                                    0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae};

Bytes header(uint16_t type, uint16_t length,
             const stun::TransactionId &id = kRfcId) {
  Bytes bytes = {uint8_t(type >> 8),
                 uint8_t(type),
                 uint8_t(length >> 8),
                 uint8_t(length),
                 0x21,
                 0x12,
                 0xA4,
                 0x42};
  bytes.insert(bytes.end(), id.begin(), id.end());
  return bytes;
}

void append(Bytes &to, const Bytes &more) {
  to.insert(to.end(), more.begin(), more.end());
}

bool parse(const Bytes &bytes, Address &mapped,
           stun::TransactionId *id = nullptr) {
  stun::TransactionId scratch;
  return stun::parseBindingResponse(bytes.data(), bytes.size(),
                                    id ? *id : scratch, mapped);
}

} // namespace

// ---- The STUN message codec ----

TEST(Stun, RequestIsTheTwentyByteBindingRequest) {
  Bytes request = stun::makeBindingRequest(kRfcId);
  Bytes expected = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42, 0xb7, 0xe7,
                    0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae};
  EXPECT_EQ(request, expected);
  EXPECT_TRUE(stun::looksLikeStun(request.data(), request.size()));
}

// The IPv4 response of RFC 5769 section 2.2 (without its integrity and
// fingerprint attributes, which are not read): 192.0.2.1:32853.
TEST(Stun, ReadsTheRfcIPv4Response) {
  Bytes message = header(0x0101, 12 + 16);
  append(message, {0x80, 0x22, 0x00, 0x0b, 't', 'e', 's', 't', ' ', 'v', 'e',
                   'c', 't', 'o', 'r', ' '}); // SOFTWARE, 11 bytes, padded
  append(message, {0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12,
                   0xa6, 0x43});
  Address mapped;
  stun::TransactionId id{};
  ASSERT_TRUE(parse(message, mapped, &id));
  EXPECT_EQ(mapped, Address::ipv4(192, 0, 2, 1, 32853));
  EXPECT_EQ(id, kRfcId);
}

// RFC 5769 section 2.3: 2001:db8:1234:5678:11:2233:4455:6677, port 32853.
TEST(Stun, ReadsTheRfcIPv6Response) {
  Bytes message = header(0x0101, 4 + 20);
  append(message, {0x00, 0x20, 0x00, 0x14, 0x00, 0x02, 0xa1, 0x47,
                   0x01, 0x13, 0xa9, 0xfa, 0xa5, 0xd3, 0xf1, 0x79,
                   0xbc, 0x25, 0xf4, 0xb5, 0xbe, 0xd2, 0xb9, 0xd9});
  Address mapped;
  ASSERT_TRUE(parse(message, mapped));
  Address expected;
  ASSERT_TRUE(
      Address::parse("[2001:db8:1234:5678:11:2233:4455:6677]:32853", expected));
  EXPECT_EQ(mapped, expected);
}

TEST(Stun, ResponsesRoundTrip) {
  Address v4 = Address::ipv4(203, 0, 113, 9, 61000);
  Address v6;
  ASSERT_TRUE(Address::parse("[2001:db8::7]:4242", v6));
  for (const Address &address : {v4, v6}) {
    Bytes response = stun::makeBindingResponse(kRfcId, address);
    Address mapped;
    stun::TransactionId id{};
    ASSERT_TRUE(parse(response, mapped, &id)) << address.toString();
    EXPECT_EQ(mapped, address);
    EXPECT_EQ(id, kRfcId);
  }
}

TEST(Stun, LegacyMappedAddressIsUsedWhenItIsAllThereIs) {
  Bytes message = header(0x0101, 12);
  append(message,
         {0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x1F, 0x90, 198, 51, 100, 7});
  Address mapped;
  ASSERT_TRUE(parse(message, mapped));
  EXPECT_EQ(mapped, Address::ipv4(198, 51, 100, 7, 8080));
}

TEST(Stun, TheXorAddressWinsOverTheLegacyOne) {
  Bytes message = header(0x0101, 12 + 12);
  append(message, {0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x1F, 0x90, 1, 1, 1, 1});
  append(message, {0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12,
                   0xa6, 0x43});
  Address mapped;
  ASSERT_TRUE(parse(message, mapped));
  EXPECT_EQ(mapped, Address::ipv4(192, 0, 2, 1, 32853));
}

TEST(Stun, MalformedOrUnusableMessagesAreRejected) {
  Bytes good = stun::makeBindingResponse(kRfcId, Address::ipv4(1, 2, 3, 4, 5));
  Address mapped;
  ASSERT_TRUE(parse(good, mapped));

  auto mutated = [&](auto change) {
    Bytes copy = good;
    change(copy);
    return copy;
  };
  std::vector<std::pair<const char *, Bytes>> bad = {
      {"error response", mutated([](Bytes &b) {
         b[0] = 0x01;
         b[1] = 0x11;
       })},
      {"a request", mutated([](Bytes &b) {
         b[0] = 0x00;
         b[1] = 0x01;
       })},
      {"wrong cookie", mutated([](Bytes &b) { b[4] = 0x00; })},
      {"top bits set", mutated([](Bytes &b) { b[0] |= 0x80; })},
      {"length too long", mutated([](Bytes &b) { b[3] += 4; })},
      {"length too short", mutated([](Bytes &b) { b[3] -= 4; })},
      {"length not a multiple of four", mutated([](Bytes &b) { b[3] -= 1; })},
      {"truncated", mutated([](Bytes &b) { b.pop_back(); })},
      {"extra byte", mutated([](Bytes &b) { b.push_back(0); })},
      {"unknown address family", mutated([](Bytes &b) { b[25] = 0x07; })},
      {"address too short for its family",
       mutated([](Bytes &b) { b[23] = 0x04; })},
      {"too small for a header", Bytes(good.begin(), good.begin() + 12)},
      {"empty", Bytes()},
  };
  for (const auto &entry : bad) {
    Address out = Address::ipv4(9, 9, 9, 9, 9);
    EXPECT_FALSE(parse(entry.second, out)) << entry.first;
  }

  // No address at all.
  Bytes empty = header(0x0101, 0);
  EXPECT_FALSE(parse(empty, mapped));
  // An attribute that claims more than there is.
  Bytes overrun = header(0x0101, 8);
  append(overrun, {0x80, 0x22, 0x00, 0x40, 0, 0, 0, 0});
  EXPECT_FALSE(parse(overrun, mapped));
  // Port 0 is not an address anyone can send to.
  Bytes zeroPort = header(0x0101, 12);
  append(zeroPort, {0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0x21, 0x12, 0xe1, 0x12,
                    0xa6, 0x43});
  EXPECT_FALSE(parse(zeroPort, mapped));
}

TEST(Stun, GarbageNeverCrashesTheParser) {
  uint32_t state = 7;
  auto next = [&] {
    state = state * 1664525u + 1013904223u;
    return static_cast<uint8_t>(state >> 24);
  };
  Bytes base = stun::makeBindingResponse(kRfcId, Address::ipv4(1, 2, 3, 4, 5));
  for (int round = 0; round < 5000; ++round) {
    Bytes junk = base;
    // Keep it STUN-shaped so the attribute parser is what gets exercised.
    for (int i = 0; i < 4; ++i) {
      junk[next() % junk.size()] = next();
    }
    junk.resize(20 + next() % 40);
    Address mapped;
    parse(junk, mapped);
  }
}

TEST(Stun, TransportPacketsDoNotLookLikeStun) {
  Bytes data = {0x01, 0x50, 0x45, 0x43, 0x04, 1,  2,  3,  4,  5,  6,  7, 8,
                9,    10,   11,   12,   13,   14, 15, 16, 17, 18, 19, 20};
  EXPECT_FALSE(stun::looksLikeStun(data.data(), data.size()));
}

// ---- Invite codes ----

TEST(InviteCode, RoundTripsAddresses) {
  Address v6;
  ASSERT_TRUE(Address::parse("[2001:db8:85a3::8a2e:370:7334]:65535", v6));
  for (const Address &address :
       {Address::ipv4(194, 12, 8, 40, 5000), Address::ipv4(0, 0, 0, 0, 0),
        Address::ipv4(255, 255, 255, 255, 65535), v6}) {
    std::string code = encodeInvite(address);
    Address decoded;
    ASSERT_TRUE(decodeInvite(code, decoded)) << code;
    EXPECT_EQ(decoded, address) << code;
  }
  EXPECT_EQ(encodeInvite(Address()), "");
}

TEST(InviteCode, LooksLikeShortGroupsOfLettersAndDigits) {
  EXPECT_EQ(encodeInvite(Address::ipv4(194, 12, 8, 40, 5000)),
            "0K10-R218-2E45-0");
  std::string code = encodeInvite(Address::ipv4(194, 12, 8, 40, 5000));
  EXPECT_EQ(code.size(), 16u); // 13 characters in groups of four
  for (size_t i = 0; i < code.size(); ++i) {
    if (i % 5 == 4) {
      EXPECT_EQ(code[i], '-');
    } else {
      EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(code[i])));
      EXPECT_FALSE(std::string("ILOU").find(code[i]) != std::string::npos);
    }
  }
}

TEST(InviteCode, ToleratesHowPeopleTypeThem) {
  Address address = Address::ipv4(10, 20, 30, 40, 1234);
  std::string code = encodeInvite(address);
  Address decoded;
  std::string lower = code;
  for (char &c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  ASSERT_TRUE(decodeInvite(lower, decoded));
  EXPECT_EQ(decoded, address);
  std::string spaced = code;
  for (char &c : spaced) {
    if (c == '-') {
      c = ' ';
    }
  }
  ASSERT_TRUE(decodeInvite("  " + spaced + "\n", decoded));
  EXPECT_EQ(decoded, address);
  std::string bare;
  for (char c : code) {
    if (c != '-') {
      bare.push_back(c);
    }
  }
  ASSERT_TRUE(decodeInvite(bare, decoded));
  EXPECT_EQ(decoded, address);
  std::string misread = code;
  for (char &c : misread) {
    c = c == '0' ? 'O' : c == '1' ? 'l' : c;
  }
  ASSERT_TRUE(decodeInvite(misread, decoded));
  EXPECT_EQ(decoded, address);
}

TEST(InviteCode, EveryWrongCharacterIsCaught) {
  Address address = Address::ipv4(203, 0, 113, 77, 40000);
  std::string code = encodeInvite(address);
  const std::string alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
  int tried = 0;
  for (size_t i = 0; i < code.size(); ++i) {
    if (code[i] == '-') {
      continue;
    }
    for (char replacement : alphabet) {
      if (replacement == code[i]) {
        continue;
      }
      std::string wrong = code;
      wrong[i] = replacement;
      Address decoded;
      ++tried;
      EXPECT_FALSE(decodeInvite(wrong, decoded))
          << wrong << " (was " << code << ")";
    }
  }
  EXPECT_GT(tried, 300);
}

TEST(InviteCode, RejectsWhatIsNotACode) {
  Address address = Address::ipv4(1, 2, 3, 4, 5);
  std::string code = encodeInvite(address);
  Address out;
  for (const std::string &bad :
       {std::string(), std::string("hello world"), std::string("----"),
        std::string("U000-0000"), // U is not in the alphabet
        code.substr(0, code.size() - 2), code + "0", code + "00000",
        std::string("0000-0000-0000-0000-00")}) {
    EXPECT_FALSE(decodeInvite(bad, out)) << "'" << bad << "'";
  }
}

// ---- Looking names up ----

TEST(Address, ResolvesNamesAndNumbers) {
  Address address;
  ASSERT_TRUE(
      Address::resolve("localhost", 3478, Address::Family::IPv4, address));
  EXPECT_EQ(address, Address::loopback(3478));
  ASSERT_TRUE(Address::resolve("127.0.0.1", 9, Address::Family::IPv4, address));
  EXPECT_EQ(address, Address::loopback(9));
  EXPECT_FALSE(Address::resolve("", 1, Address::Family::IPv4, address));
  EXPECT_FALSE(Address::resolve("no-such-host.invalid", 1,
                                Address::Family::IPv4, address));
  EXPECT_FALSE(
      Address::resolve("localhost", 1, Address::Family::None, address));
}

// ---- Working out the public address ----

namespace {

// Drives a NatDiscovery against mock servers on a simulated internet, with no
// sockets of its own: what it would send is delivered directly.
struct DiscoveryRig {
  LoopbackNetwork internet;
  nat_sim::MockStunServer one;
  nat_sim::MockStunServer two;
  std::unique_ptr<LoopbackSocket> client;
  NatDiscovery discovery{1};
  std::vector<double> sendTimes;

  DiscoveryRig()
      : one(internet, 3478), two(internet, 3479), client(internet.open()) {
  }

  void run(double from, double to, double dt = 0.05) {
    for (double t = from; t < to; t += dt) {
      discovery.update(
          t, [&](const Address &server, const uint8_t *data, size_t size) {
            sendTimes.push_back(t);
            client->sendTo(server, data, size);
          });
      one.update();
      two.update();
      uint8_t buffer[kMaxPacketSize];
      size_t received = 0;
      Address from;
      while (client->recvFrom(buffer, sizeof(buffer), received, from) ==
             IoStatus::Ok) {
        discovery.handleDatagram(from, buffer, received);
      }
    }
  }
};

} // namespace

TEST(NatDiscovery, LearnsThePublicAddressFromOneServer) {
  DiscoveryRig rig;
  EXPECT_EQ(rig.discovery.state(), NatDiscovery::State::Idle);
  rig.discovery.start({rig.one.address()});
  EXPECT_EQ(rig.discovery.state(), NatDiscovery::State::Querying);
  rig.run(0.0, 1.0);
  EXPECT_EQ(rig.discovery.state(), NatDiscovery::State::Done);
  EXPECT_EQ(rig.discovery.info().publicAddress,
            Address::loopback(rig.client->localAddress().port()));
  EXPECT_EQ(rig.discovery.info().type, NatType::Unknown);
}

TEST(NatDiscovery, TwoServersThatAgreeMeanACone) {
  DiscoveryRig rig;
  rig.discovery.start({rig.one.address(), rig.two.address()});
  rig.run(0.0, 1.0);
  EXPECT_EQ(rig.discovery.state(), NatDiscovery::State::Done);
  EXPECT_EQ(rig.discovery.info().type, NatType::Cone);
}

TEST(NatDiscovery, TwoServersThatDisagreeMeanSymmetric) {
  // A symmetric NAT: the client's address differs by destination. Simulated by
  // sending from a different socket to each server.
  LoopbackNetwork internet;
  nat_sim::NatSocket nat(internet, nat_sim::NatKind::Symmetric, 5000);
  nat_sim::MockStunServer one(internet, 3478), two(internet, 3479);
  NatDiscovery discovery(1);
  discovery.start({one.address(), two.address()});
  for (double t = 0.0; t < 1.0; t += 0.05) {
    discovery.update(t, [&](const Address &to, const uint8_t *d, size_t n) {
      nat.sendTo(to, d, n);
    });
    one.update();
    two.update();
    uint8_t buffer[kMaxPacketSize];
    size_t received = 0;
    Address from;
    while (nat.recvFrom(buffer, sizeof(buffer), received, from) ==
           IoStatus::Ok) {
      discovery.handleDatagram(from, buffer, received);
    }
  }
  EXPECT_EQ(discovery.state(), NatDiscovery::State::Done);
  EXPECT_EQ(discovery.info().type, NatType::Symmetric);
}

TEST(NatDiscovery, RetriesLostRequestsThenGivesUp) {
  DiscoveryRig rig;
  rig.one.silent = true;
  rig.discovery.start({rig.one.address()});
  rig.run(0.0, 5.0);
  EXPECT_EQ(rig.discovery.state(), NatDiscovery::State::Querying);
  rig.run(5.0, 6.0);
  EXPECT_EQ(rig.discovery.state(), NatDiscovery::State::Failed);
  EXPECT_FALSE(rig.discovery.info().publicAddress.isValid());
  // Four requests, at 0, 0.5, 1.5 and 3.5 seconds.
  ASSERT_EQ(rig.sendTimes.size(), 4u);
  EXPECT_NEAR(rig.sendTimes[0], 0.0, 0.15);
  EXPECT_NEAR(rig.sendTimes[1], 0.5, 0.15);
  EXPECT_NEAR(rig.sendTimes[2], 1.5, 0.15);
  EXPECT_NEAR(rig.sendTimes[3], 3.5, 0.15);
}

TEST(NatDiscovery, ALostRequestIsRecovered) {
  DiscoveryRig rig;
  rig.one.ignoreFirst = 2;
  rig.discovery.start({rig.one.address()});
  rig.run(0.0, 4.0);
  EXPECT_EQ(rig.discovery.state(), NatDiscovery::State::Done);
  EXPECT_TRUE(rig.discovery.info().publicAddress.isValid());
}

TEST(NatDiscovery, OneServerDownStillGivesAnAddress) {
  DiscoveryRig rig;
  rig.two.silent = true;
  rig.discovery.start({rig.one.address(), rig.two.address()});
  rig.run(0.0, 1.0);
  EXPECT_EQ(rig.discovery.state(), NatDiscovery::State::Querying);
  EXPECT_TRUE(rig.discovery.info().publicAddress.isValid());
  rig.run(1.0, 7.0);
  EXPECT_EQ(rig.discovery.state(), NatDiscovery::State::Done);
  EXPECT_EQ(rig.discovery.info().type, NatType::Unknown);
}

TEST(NatDiscovery, NoServersOrBadOnesFailAtOnce) {
  NatDiscovery discovery(1);
  discovery.start({});
  EXPECT_EQ(discovery.state(), NatDiscovery::State::Failed);
  discovery.start({Address(), Address()});
  EXPECT_EQ(discovery.state(), NatDiscovery::State::Failed);
}

TEST(NatDiscovery, AnswersFromTheWrongPlaceOrForTheWrongRequestAreNotTaken) {
  LoopbackNetwork internet;
  nat_sim::MockStunServer server(internet, 3478);
  NatDiscovery discovery(1);
  discovery.start({server.address()});

  stun::TransactionId other{};
  Bytes forWrongId =
      stun::makeBindingResponse(other, Address::ipv4(6, 6, 6, 6, 6));
  EXPECT_FALSE(discovery.handleDatagram(server.address(), forWrongId.data(),
                                        forWrongId.size()));

  // Capture the real request to answer it, but from the wrong address.
  std::vector<uint8_t> sent;
  discovery.update(0.0, [&](const Address &, const uint8_t *d, size_t n) {
    sent.assign(d, d + n);
  });
  ASSERT_EQ(sent.size(), 20u);
  stun::TransactionId id;
  std::copy(sent.begin() + 8, sent.begin() + 20, id.begin());
  Bytes answer = stun::makeBindingResponse(id, Address::ipv4(6, 6, 6, 6, 6));
  EXPECT_FALSE(discovery.handleDatagram(Address::ipv4(1, 1, 1, 1, 1),
                                        answer.data(), answer.size()));
  EXPECT_FALSE(discovery.info().publicAddress.isValid());
  EXPECT_TRUE(
      discovery.handleDatagram(server.address(), answer.data(), answer.size()));
  EXPECT_EQ(discovery.info().publicAddress, Address::ipv4(6, 6, 6, 6, 6));
}

TEST(NatDiscovery, AtMostThreeServersAreUsed) {
  NatDiscovery discovery(1);
  discovery.start({Address::ipv4(1, 1, 1, 1, 1), Address::ipv4(1, 1, 1, 2, 1),
                   Address::ipv4(1, 1, 1, 3, 1), Address::ipv4(1, 1, 1, 4, 1),
                   Address::ipv4(1, 1, 1, 1, 1)});
  std::vector<Address> asked;
  discovery.update(0.0, [&](const Address &to, const uint8_t *, size_t) {
    asked.push_back(to);
  });
  EXPECT_EQ(asked.size(), 3u);
}

// ---- Sharing the game's socket ----

TEST(StunSocket, KeepsStunAnswersFromTheReaderAndPassesTheRest) {
  LoopbackNetwork internet;
  nat_sim::MockStunServer server(internet, 3478);
  StunSocket socket(internet.open(), 1);
  auto friendSocket = internet.open();
  Address mine = socket.localAddress();

  socket.discovery().start({server.address()});
  socket.update(0.0);
  server.update();
  friendSocket->sendTo(mine, "before", 6);
  friendSocket->sendTo(mine, "after", 5);

  std::vector<std::string> seen;
  uint8_t buffer[kMaxPacketSize];
  size_t received = 0;
  Address from;
  while (socket.recvFrom(buffer, sizeof(buffer), received, from) ==
         IoStatus::Ok) {
    seen.emplace_back(reinterpret_cast<char *>(buffer), received);
  }
  EXPECT_EQ(seen, (std::vector<std::string>{"before", "after"}));
  EXPECT_EQ(socket.discovery().state(), NatDiscovery::State::Done);
  EXPECT_EQ(socket.discovery().info().publicAddress, mine);
}

TEST(StunSocket, ADatagramThatOnlyLooksLikeStunPassesThrough) {
  LoopbackNetwork internet;
  StunSocket socket(internet.open(), 1);
  auto other = internet.open();
  socket.discovery().start({Address::loopback(3478)});
  Bytes stray = stun::makeBindingResponse(kRfcId, Address::ipv4(1, 2, 3, 4, 5));
  other->sendTo(socket.localAddress(), stray.data(), stray.size());
  uint8_t buffer[kMaxPacketSize];
  size_t received = 0;
  Address from;
  ASSERT_EQ(socket.recvFrom(buffer, sizeof(buffer), received, from),
            IoStatus::Ok);
  EXPECT_EQ(received, stray.size());
}

// ---- Punching ----

TEST(Punch, SendsHarmlessPacketsUntilTheTimeIsUp) {
  LoopbackNetwork internet;
  auto socket = internet.open();
  auto target = internet.open();
  TransportConfig config;
  config.seed = 3;
  Transport transport(*socket, config);
  transport.punch(target->localAddress(), 2.0);

  int count = 0;
  Bytes first;
  auto drain = [&] {
    uint8_t buffer[kMaxPacketSize];
    size_t received = 0;
    Address from;
    while (target->recvFrom(buffer, sizeof(buffer), received, from) ==
           IoStatus::Ok) {
      if (count++ == 0) {
        first.assign(buffer, buffer + received);
      }
    }
  };
  for (double t = 0.0; t < 1.9; t += 0.05) {
    transport.update(t);
    drain();
  }
  EXPECT_GE(count, 7);
  EXPECT_LE(count, 9);
  EXPECT_EQ(first.size(), 5u); // the protocol id and a type byte
  int atExpiry = count;
  for (double t = 1.9; t < 5.0; t += 0.05) {
    transport.update(t);
    drain();
  }
  EXPECT_LE(count, atExpiry + 1);
}

TEST(Punch, StopsOnceTheConnectionIsUp) {
  LoopbackNetwork internet;
  auto socketA = internet.open();
  auto socketB = internet.open();
  TransportConfig configA, configB;
  configA.seed = 1;
  configB.seed = 2;
  Transport a(*socketA, configA), b(*socketB, configB);
  a.setAcceptIncoming(true);
  a.punch(socketB->localAddress(), 60.0);
  ASSERT_TRUE(b.connect(socketA->localAddress()));
  double t = 0.0;
  for (; t < 3.0 && !(a.isConnected(socketB->localAddress())); t += 0.01) {
    a.update(t);
    b.update(t);
  }
  ASSERT_TRUE(a.isConnected(socketB->localAddress()));
  uint64_t before = a.stats(socketB->localAddress()).packetsSent;
  (void)before;
  Event event;
  while (a.pollEvent(event)) {
  }
  for (double end = t + 1.0; t < end; t += 0.01) {
    a.update(t);
    b.update(t);
  }
  EXPECT_TRUE(a.isConnected(socketB->localAddress()));
  EXPECT_FALSE(a.pollEvent(event)); // the stray packets caused no events
}

// ---- Network: finding its own public address ----

namespace {

struct NetworkRig {
  LoopbackNetwork internet;
  nat_sim::MockStunServer one;
  nat_sim::MockStunServer two;
  Network net;
  NatTraversal nat{net};
  double now = 0.0;
  bool callbackFired = false;
  bool callbackOk = false;
  NatInfo callbackInfo;

  NetworkRig() : one(internet, 3478), two(internet, 3479) {
    EXPECT_TRUE(net.open(internet.open(), testConfig()));
    nat.onDiscovery([this](bool ok, const NatInfo &info) {
      callbackFired = true;
      callbackOk = ok;
      callbackInfo = info;
    });
  }
  void step(double dt = 0.01, bool realSleep = false) {
    now += dt;
    one.update();
    two.update();
    net.update(now);
    if (realSleep) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  bool runUntil(const std::function<bool()> &done, double seconds,
                bool realSleep = false) {
    for (double end = now + seconds; now < end;) {
      if (done()) {
        return true;
      }
      step(0.01, realSleep);
    }
    return done();
  }
};

} // namespace

TEST(NetworkNat, DiscoversItsPublicAddressFromNumericServers) {
  NetworkRig rig;
  EXPECT_FALSE(rig.nat.publicAddress().isValid());
  EXPECT_EQ(rig.nat.discoveryState(), NatDiscovery::State::Idle);
  EXPECT_EQ(rig.nat.inviteCode(), "");

  rig.nat.discoverPublicAddress({"127.0.0.1:3478", "127.0.0.1:3479"});
  ASSERT_TRUE(rig.runUntil([&] { return rig.callbackFired; }, 10.0, true));
  EXPECT_TRUE(rig.callbackOk);
  Address expected = Address::loopback(rig.net.localAddress().port());
  EXPECT_EQ(rig.nat.publicAddress(), expected);
  EXPECT_EQ(rig.callbackInfo.publicAddress, expected);
  EXPECT_EQ(rig.nat.natType(), NatType::Cone);
  Address decoded;
  ASSERT_TRUE(decodeInvite(rig.nat.inviteCode(), decoded));
  EXPECT_EQ(decoded, expected);
}

TEST(NetworkNat, ResolvesServerNamesInTheBackground) {
  NetworkRig rig;
  rig.nat.discoverPublicAddress({"localhost:3478"});
  ASSERT_TRUE(rig.runUntil([&] { return rig.callbackFired; }, 10.0, true));
  EXPECT_TRUE(rig.callbackOk);
  EXPECT_EQ(rig.nat.publicAddress(),
            Address::loopback(rig.net.localAddress().port()));
}

TEST(NetworkNat, ReportsFailureWhenNoServerAnswers) {
  NetworkRig rig;
  rig.one.silent = true;
  rig.nat.discoverPublicAddress({"127.0.0.1:3478"});
  ASSERT_TRUE(rig.runUntil([&] { return rig.callbackFired; }, 15.0, true));
  EXPECT_FALSE(rig.callbackOk);
  EXPECT_FALSE(rig.nat.publicAddress().isValid());
  EXPECT_EQ(rig.nat.discoveryState(), NatDiscovery::State::Failed);
  EXPECT_EQ(rig.nat.inviteCode(), "");
}

TEST(NetworkNat, ReportsFailureForNamesThatDoNotResolve) {
  NetworkRig rig;
  rig.nat.discoverPublicAddress({"no-such-host.invalid:3478", "garbage", ":5"});
  ASSERT_TRUE(rig.runUntil([&] { return rig.callbackFired; }, 20.0, true));
  EXPECT_FALSE(rig.callbackOk);
}

TEST(NetworkNat, ClosingWhileLookingUpIsSafe) {
  NetworkRig rig;
  rig.nat.discoverPublicAddress({"no-such-host.invalid:3478"});
  rig.net.close();
  rig.step();
  EXPECT_FALSE(rig.nat.publicAddress().isValid());
  EXPECT_EQ(rig.nat.discoveryState(), NatDiscovery::State::Idle);
  rig.nat.discoverPublicAddress();
  rig.net.punch(Address::loopback(1));
  EXPECT_FALSE(rig.nat.connectToInvite(encodeInvite(Address::loopback(1))));
}

TEST(NetworkNat, TheStunTrafficDoesNotDisturbTheGame) {
  NetworkRig rig;
  Network other;
  ASSERT_TRUE(other.open(rig.internet.open(), testConfig()));
  other.setAcceptIncoming(true);
  rig.nat.discoverPublicAddress({"127.0.0.1:3478"});
  ASSERT_TRUE(rig.net.connect(Address::loopback(other.localAddress().port())));
  for (int i = 0; i < 600; ++i) {
    rig.step(0.01, true); // the name lookup runs on a real thread
    other.update(rig.now);
  }
  EXPECT_TRUE(
      rig.net.isConnected(Address::loopback(other.localAddress().port())));
  EXPECT_TRUE(rig.nat.publicAddress().isValid());
  EXPECT_EQ(rig.net.rejectedMessages(), 0u);
}

TEST(NetworkNat, ConnectToInviteRejectsBadCodes) {
  NetworkRig rig;
  EXPECT_FALSE(rig.nat.connectToInvite("not a code"));
  EXPECT_FALSE(rig.nat.connectToInvite(""));
}
