#include "componeng/net/archive.hpp"
#include "componeng/net/lan_discovery.hpp"
#include "componeng/net/loopback_socket.hpp"
#include "gtest/gtest.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace componeng::net;
using Bytes = std::vector<uint8_t>;

namespace {

constexpr uint32_t kProtocol = 0xABCD0001;
constexpr uint16_t kPort = 47777;

// Discovery sockets that live on a simulated network, so nothing here depends
// on the machine's real network.
struct Lan {
  LoopbackNetwork network;
  LanSocketFactory factory() {
    return [this](uint16_t port, bool) -> std::unique_ptr<IDatagramSocket> {
      return network.open(port);
    };
  }
};

LanAdvert advert(uint32_t id, const std::string &name, uint16_t gamePort = 7000,
                 uint16_t peers = 1, uint16_t maxPeers = 4) {
  LanAdvert a;
  a.sessionId = id;
  a.name = name;
  a.gamePort = gamePort;
  a.peers = peers;
  a.maxPeers = maxPeers;
  return a;
}

Bytes discoverPacket(uint32_t protocolId, size_t size = kDiscoverPacketSize) {
  ByteWriter writer;
  writer(protocolId, uint8_t{1});
  Bytes bytes = writer.release();
  bytes.resize(size, 0);
  return bytes;
}

} // namespace

TEST(LanDiscovery, BrowserFindsAnAdvertisedSession) {
  Lan lan;
  LanAdvertiser host;
  host.start(kProtocol, kPort, lan.factory());
  host.setAdvert(advert(42, "Jigsaw night", 7000, 2, 6));
  LanBrowser browser;
  ASSERT_TRUE(browser.start(kProtocol, kPort, lan.factory()));

  for (double t = 0.0; t < 0.5; t += 0.01) {
    browser.update(t);
    host.update(t);
  }
  auto sessions = browser.sessions();
  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_EQ(sessions[0].sessionId, 42u);
  EXPECT_EQ(sessions[0].name, "Jigsaw night");
  EXPECT_EQ(sessions[0].peers, 2);
  EXPECT_EQ(sessions[0].maxPeers, 6);
  EXPECT_EQ(sessions[0].address, Address::loopback(7000));
}

TEST(LanDiscovery, NothingIsSentUnlessSomeoneAsks) {
  Lan lan;
  LanAdvertiser host;
  host.start(kProtocol, kPort, lan.factory());
  host.setAdvert(advert(1, "quiet"));
  auto listener = lan.network.open();
  for (double t = 0.0; t < 3.0; t += 0.05) {
    host.update(t);
  }
  EXPECT_FALSE(listener->waitReadable(0));
}

TEST(LanDiscovery, OtherGamesAreInvisible) {
  Lan lan;
  LanAdvertiser host;
  host.start(kProtocol, kPort, lan.factory());
  host.setAdvert(advert(1, "mine"));
  LanBrowser otherGame;
  ASSERT_TRUE(otherGame.start(kProtocol + 1, kPort, lan.factory()));
  for (double t = 0.0; t < 2.0; t += 0.01) {
    otherGame.update(t);
    host.update(t);
  }
  EXPECT_TRUE(otherGame.sessions().empty());
}

TEST(LanDiscovery, SessionsComeAndGoAndChange) {
  Lan lan;
  LanAdvertiser host;
  host.start(kProtocol, kPort, lan.factory());
  host.setAdvert(advert(7, "room", 7000, 1));
  LanBrowser browser;
  ASSERT_TRUE(browser.start(kProtocol, kPort, lan.factory()));

  double t = 0.0;
  auto run = [&](double seconds) {
    for (double end = t + seconds; t < end; t += 0.01) {
      browser.update(t);
      host.update(t);
    }
  };
  run(1.5);
  ASSERT_EQ(browser.sessions().size(), 1u);
  EXPECT_EQ(browser.sessions()[0].peers, 1);

  host.setAdvert(advert(7, "room", 7000, 3));
  run(1.5);
  ASSERT_EQ(browser.sessions().size(), 1u);
  EXPECT_EQ(browser.sessions()[0].peers, 3);

  host.stop();
  run(LanBrowser::kSessionExpiry + 1.5);
  EXPECT_TRUE(browser.sessions().empty());
}

TEST(LanDiscovery, SessionThatMovesToANewHostIsOneEntry) {
  Lan lan;
  LanAdvertiser first;
  first.start(kProtocol, kPort, lan.factory());
  first.setAdvert(advert(9, "room", 7000));
  LanBrowser browser;
  ASSERT_TRUE(browser.start(kProtocol, kPort, lan.factory()));
  double t = 0.0;
  auto run = [&](double seconds) {
    for (double end = t + seconds; t < end; t += 0.01) {
      browser.update(t);
      first.update(t);
    }
  };
  run(1.5);
  first.stop();
  LanAdvertiser second;
  second.start(kProtocol, kPort, lan.factory());
  second.setAdvert(advert(9, "room", 7100));
  for (double end = t + 1.5; t < end; t += 0.01) {
    browser.update(t);
    second.update(t);
  }
  auto sessions = browser.sessions();
  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_EQ(sessions[0].address, Address::loopback(7100));
}

TEST(LanDiscovery, AnswerIsNeverBiggerThanTheRequest) {
  Lan lan;
  LanAdvertiser host;
  host.start(kProtocol, kPort, lan.factory());
  // A name far past the limit: it is cut, so the answer stays small.
  host.setAdvert(advert(1, std::string(500, 'n')));
  auto victim = lan.network.open();

  Bytes request = discoverPacket(kProtocol);
  ASSERT_EQ(
      victim->sendTo(Address::loopback(kPort), request.data(), request.size()),
      IoStatus::Ok);
  host.update(0.0);
  uint8_t buffer[kMaxPacketSize];
  size_t received = 0;
  Address from;
  ASSERT_EQ(victim->recvFrom(buffer, sizeof(buffer), received, from),
            IoStatus::Ok);
  EXPECT_LE(received, request.size());
}

TEST(LanDiscovery, ShortOrMalformedRequestsGetNoAnswer) {
  Lan lan;
  LanAdvertiser host;
  host.start(kProtocol, kPort, lan.factory());
  host.setAdvert(advert(1, "x"));
  auto probe = lan.network.open();
  std::vector<Bytes> requests = {
      discoverPacket(kProtocol, kDiscoverPacketSize - 1),
      discoverPacket(kProtocol, 5),
      discoverPacket(kProtocol + 1),
      Bytes(kDiscoverPacketSize, 0xFF),
      Bytes(),
  };
  for (const Bytes &request : requests) {
    probe->sendTo(Address::loopback(kPort), request.data(), request.size());
  }
  host.update(0.0);
  EXPECT_FALSE(probe->waitReadable(0));
}

TEST(LanDiscovery, BrowserIgnoresJunkAnnouncements) {
  Lan lan;
  // A stand-in host that also lets us learn the browser's own address.
  auto fakeHost = lan.network.open(kPort);
  LanBrowser browser;
  ASSERT_TRUE(browser.start(kProtocol, kPort, lan.factory()));
  browser.update(0.0);
  Address browserAddress;
  uint8_t buffer[kMaxPacketSize];
  size_t received = 0;
  ASSERT_EQ(
      fakeHost->recvFrom(buffer, sizeof(buffer), received, browserAddress),
      IoStatus::Ok);

  auto announce = [&](uint32_t protocol, uint8_t kind,
                      const std::string &name) {
    ByteWriter w;
    w(protocol, kind, uint32_t{5}, uint16_t{7000}, uint16_t{1}, uint16_t{4},
      name);
    return w.release();
  };
  std::vector<Bytes> junk = {
      announce(kProtocol + 1, 2, "wrong game"),
      announce(kProtocol, 1, "wrong kind"),
      announce(kProtocol, 2, std::string(kMaxSessionNameLength + 1, 'x')),
      Bytes{1, 2, 3},
      Bytes(),
  };
  Bytes truncated = announce(kProtocol, 2, "cut");
  truncated.resize(truncated.size() - 2);
  junk.push_back(truncated);
  for (const Bytes &packet : junk) {
    fakeHost->sendTo(browserAddress, packet.data(), packet.size());
  }
  browser.update(0.1);
  EXPECT_TRUE(browser.sessions().empty());

  Bytes good = announce(kProtocol, 2, "fine");
  fakeHost->sendTo(browserAddress, good.data(), good.size());
  browser.update(0.2);
  EXPECT_EQ(browser.sessions().size(), 1u);
}

TEST(LanDiscovery, AdvertiserKeepsTryingWhilePortIsBusy) {
  Lan lan;
  auto squatter = lan.network.open(kPort);
  LanAdvertiser host;
  host.start(kProtocol, kPort, lan.factory());
  EXPECT_TRUE(host.isRunning());
  EXPECT_FALSE(host.isListening());

  squatter.reset();
  host.update(0.1);
  EXPECT_FALSE(host.isListening()); // not before the retry interval
  host.update(1.5);
  EXPECT_TRUE(host.isListening());
}

TEST(LanDiscovery, BrowserStartFailsWithoutASocket) {
  LanBrowser browser;
  EXPECT_FALSE(browser.start(
      kProtocol, kPort, [](uint16_t, bool) -> std::unique_ptr<IDatagramSocket> {
        return nullptr;
      }));
  EXPECT_FALSE(browser.isSearching());
}

// The same, through the real network stack on this machine.
TEST(LanDiscovery, WorksOverRealUdp) {
  // Not the default port, in case a real game is running on this machine.
  uint16_t port = static_cast<uint16_t>(
      47800 +
      (std::chrono::steady_clock::now().time_since_epoch().count() % 150));
  LanAdvertiser host;
  host.start(kProtocol, port);
  if (!host.isListening()) {
    GTEST_SKIP() << "cannot open the discovery port";
  }
  host.setAdvert(advert(77, "real udp", 7001, 1, 2));
  LanBrowser browser;
  ASSERT_TRUE(browser.start(kProtocol, port));

  using Clock = std::chrono::steady_clock;
  auto start = Clock::now();
  auto now = [&] {
    return std::chrono::duration<double>(Clock::now() - start).count();
  };
  while (now() < 5.0 && browser.sessions().empty()) {
    browser.update(now());
    host.update(now());
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  auto sessions = browser.sessions();
  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_EQ(sessions[0].sessionId, 77u);
  EXPECT_EQ(sessions[0].name, "real udp");
  EXPECT_EQ(sessions[0].address.port(), 7001);
}
