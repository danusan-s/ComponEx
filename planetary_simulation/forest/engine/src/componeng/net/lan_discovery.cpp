#include "componeng/net/lan_discovery.hpp"

#include "componeng/net/archive.hpp"
#include "componeng/net/udp_socket.hpp"
#include "componeng/utils/logger.hpp"

#include <algorithm>

namespace componeng::net {

namespace {

// A discovery packet is: protocol id u32, type u8, then for an answer the
// session's fields. Requests carry nothing but padding.
enum class PacketKind : uint8_t { Discover = 1, Announce = 2 };

constexpr uint32_t kMaxAnnounceSize = kDiscoverPacketSize;

std::vector<uint8_t> makeDiscover(uint32_t protocolId) {
  ByteWriter writer(kDiscoverPacketSize);
  writer(protocolId, static_cast<uint8_t>(PacketKind::Discover));
  std::vector<uint8_t> bytes = writer.release();
  bytes.resize(kDiscoverPacketSize, 0);
  return bytes;
}

std::vector<uint8_t> makeAnnounce(uint32_t protocolId,
                                  const LanAdvert &advert) {
  ByteWriter writer;
  std::string name = advert.name.substr(0, kMaxSessionNameLength);
  writer(protocolId, static_cast<uint8_t>(PacketKind::Announce),
         advert.sessionId, advert.gamePort, advert.peers, advert.maxPeers,
         name);
  return writer.release();
}

// The ways to reach a host from here: the whole local network, and this
// machine.
std::vector<Address> searchTargets(uint16_t port) {
  return {Address::ipv4(255, 255, 255, 255, port), Address::loopback(port)};
}

} // namespace

LanSocketFactory defaultLanSocketFactory() {
  return [](uint16_t port, bool shared) -> std::unique_ptr<IDatagramSocket> {
    auto socket = std::make_unique<UdpSocket>();
    if (!socket->open(Address::any(port), shared)) {
      return nullptr;
    }
    return socket;
  };
}

// ---- LanAdvertiser ----

void LanAdvertiser::start(uint32_t protocolId, uint16_t port,
                          LanSocketFactory factory) {
  stop();
  m_running = true;
  m_protocolId = protocolId;
  m_port = port;
  m_factory = factory ? std::move(factory) : defaultLanSocketFactory();
  // If this first attempt fails, the next is a second after the first update.
  m_nextOpenAttempt = -1.0;
  m_socket = m_factory(m_port, true);
}

void LanAdvertiser::stop() {
  m_running = false;
  m_socket.reset();
}

void LanAdvertiser::update(double now) {
  if (!m_running) {
    return;
  }
  if (!m_socket) {
    if (m_nextOpenAttempt < 0.0) {
      m_nextOpenAttempt = now + 1.0;
      return;
    }
    if (now < m_nextOpenAttempt) {
      return;
    }
    m_nextOpenAttempt = now + 1.0;
    m_socket = m_factory(m_port, true);
    if (!m_socket) {
      return;
    }
  }

  uint8_t buffer[kMaxPacketSize];
  size_t received = 0;
  Address from;
  while (m_socket->recvFrom(buffer, sizeof(buffer), received, from) ==
         IoStatus::Ok) {
    // Only a full-size request is answered: see kDiscoverPacketSize.
    if (received < kDiscoverPacketSize) {
      continue;
    }
    ByteReader reader(buffer, received);
    uint32_t protocolId = 0;
    uint8_t kind = 0;
    reader(protocolId, kind);
    if (reader.failed() || protocolId != m_protocolId ||
        kind != static_cast<uint8_t>(PacketKind::Discover)) {
      continue;
    }
    std::vector<uint8_t> answer = makeAnnounce(m_protocolId, m_advert);
    if (answer.size() <= kMaxAnnounceSize) {
      m_socket->sendTo(from, answer.data(), answer.size());
    }
  }
}

// ---- LanBrowser ----

bool LanBrowser::start(uint32_t protocolId, uint16_t port,
                       LanSocketFactory factory) {
  stop();
  if (!factory) {
    factory = defaultLanSocketFactory();
  }
  // An ordinary socket: answers come back to it by unicast.
  m_socket = factory(0, false);
  if (!m_socket) {
    LOG_ERROR("Could not open a socket to search for sessions");
    return false;
  }
  m_protocolId = protocolId;
  m_port = port;
  m_sessions.clear();
  m_lastRequest = -1e300;
  return true;
}

void LanBrowser::stop() {
  m_socket.reset();
  m_sessions.clear();
}

void LanBrowser::update(double now) {
  m_now = now;
  if (!m_socket) {
    return;
  }

  if (now - m_lastRequest >= kSearchInterval) {
    m_lastRequest = now;
    std::vector<uint8_t> request = makeDiscover(m_protocolId);
    for (const Address &target : searchTargets(m_port)) {
      // An unreachable target (no network, say) is not worth reporting.
      m_socket->sendTo(target, request.data(), request.size());
    }
  }

  uint8_t buffer[kMaxPacketSize];
  size_t received = 0;
  Address from;
  while (m_socket->recvFrom(buffer, sizeof(buffer), received, from) ==
         IoStatus::Ok) {
    ByteReader reader(buffer, received);
    uint32_t protocolId = 0;
    uint8_t kind = 0;
    LanSessionInfo info;
    uint16_t gamePort = 0;
    reader(protocolId, kind);
    if (reader.failed() || protocolId != m_protocolId ||
        kind != static_cast<uint8_t>(PacketKind::Announce)) {
      continue;
    }
    reader(info.sessionId, gamePort, info.peers, info.maxPeers, info.name);
    if (reader.failed() || info.name.size() > kMaxSessionNameLength) {
      continue;
    }
    info.address = Address::fromBytes(from.family(), from.bytes(), gamePort);
    info.lastSeen = now;

    // One entry per session, kept current: a session that changes host (the
    // old one left) shows up at the new address.
    auto it = std::find_if(
        m_sessions.begin(), m_sessions.end(),
        [&](const LanSessionInfo &s) { return s.sessionId == info.sessionId; });
    if (it == m_sessions.end()) {
      m_sessions.push_back(std::move(info));
    } else {
      *it = std::move(info);
    }
  }
}

std::vector<LanSessionInfo> LanBrowser::sessions() const {
  std::vector<LanSessionInfo> live;
  for (const LanSessionInfo &session : m_sessions) {
    if (m_now - session.lastSeen < kSessionExpiry) {
      live.push_back(session);
    }
  }
  return live;
}

} // namespace componeng::net
