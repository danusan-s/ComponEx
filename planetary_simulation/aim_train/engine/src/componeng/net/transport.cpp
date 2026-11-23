#include "componeng/net/transport.hpp"

#include "componeng/utils/logger.hpp"
#include "connection.hpp"
#include "packet_io.hpp"

#include <utility>

namespace componeng::net {

namespace {

// Disconnect and reject are unacked, so send a few copies.
constexpr int kDisconnectRepeats = 3;
constexpr double kPunchInterval = 0.25;

} // namespace

Transport::Transport(IDatagramSocket &socket, const TransportConfig &config)
    : m_socket(socket), m_config(config),
      m_rng(config.seed != 0 ? config.seed : std::random_device{}()) {
}

Transport::~Transport() {
  for (auto &entry : m_connections) {
    Connection &connection = *entry.second;
    if (connection.state == Connection::State::Connected) {
      sendDisconnect(entry.first, connection.id());
    }
  }
}

uint32_t Transport::newSalt() {
  // 0 means "not known yet".
  uint32_t salt = 0;
  while (salt == 0) {
    salt = m_rng();
  }
  return salt;
}

bool Transport::connect(const Address &remote) {
  if (!remote.isValid() || m_connections.count(remote) != 0 ||
      m_connections.size() >= m_config.maxConnections) {
    return false;
  }
  // Sent on the next update(), which starts the timeout clock (no time here).
  m_connections.emplace(remote, std::make_unique<Connection>(
                                    remote, Connection::State::Connecting,
                                    newSalt(), 0, m_config));
  return true;
}

void Transport::disconnect(const Address &remote) {
  auto it = m_connections.find(remote);
  if (it == m_connections.end()) {
    return;
  }
  const Connection &connection = *it->second;
  if (connection.state == Connection::State::Connected) {
    sendDisconnect(remote, connection.id());
  } else if (connection.state == Connection::State::Accepting) {
    // Refuse the peer's pending request.
    sendReject(remote, connection.remoteSalt);
  }
  removeConnection(remote, DisconnectReason::Local);
}

bool Transport::send(const Address &peer, Channel channel, const void *data,
                     size_t size) {
  auto it = m_connections.find(peer);
  if (it == m_connections.end() ||
      it->second->state != Connection::State::Connected) {
    return false;
  }
  return it->second->queue(channel, data, size);
}

bool Transport::pollEvent(Event &event) {
  if (m_events.empty()) {
    return false;
  }
  event = std::move(m_events.front());
  m_events.pop_front();
  return true;
}

bool Transport::isConnected(const Address &peer) const {
  auto it = m_connections.find(peer);
  return it != m_connections.end() &&
         it->second->state == Connection::State::Connected;
}

std::vector<Address> Transport::connectedPeers() const {
  std::vector<Address> peers;
  for (const auto &entry : m_connections) {
    if (entry.second->state == Connection::State::Connected) {
      peers.push_back(entry.first);
    }
  }
  return peers;
}

ConnectionStats Transport::stats(const Address &peer) const {
  auto it = m_connections.find(peer);
  if (it == m_connections.end()) {
    return ConnectionStats();
  }
  return it->second->stats();
}

void Transport::removeConnection(const Address &peer, DisconnectReason reason) {
  auto it = m_connections.find(peer);
  if (it == m_connections.end()) {
    return;
  }
  Connection::State state = it->second->state;
  m_connections.erase(it);

  Event event;
  event.peer = peer;
  event.reason = reason;
  if (state == Connection::State::Connected) {
    event.type = Event::Type::Disconnected;
  } else if (state == Connection::State::Connecting) {
    event.type = Event::Type::ConnectFailed;
  } else {
    return;
  }
  m_events.push_back(std::move(event));
}

void Transport::punch(const Address &to, double duration) {
  if (!to.isValid()) {
    return;
  }
  for (Punch &existing : m_punches) {
    if (existing.to == to) {
      existing.duration = duration;
      existing.until = -1.0; // starts over from the next update
      return;
    }
  }
  Punch punch;
  punch.to = to;
  punch.duration = duration;
  m_punches.push_back(punch);
}

void Transport::update(double now) {
  uint8_t buffer[kMaxPacketSize];
  size_t received = 0;
  Address from;
  while (m_socket.recvFrom(buffer, sizeof(buffer), received, from) ==
         IoStatus::Ok) {
    handlePacket(from, buffer, received, now);
  }

  // Dropped after the loop; erasing while iterating is not safe.
  std::vector<std::pair<Address, DisconnectReason>> dropped;
  for (auto &entry : m_connections) {
    const Address &peer = entry.first;
    Connection &connection = *entry.second;

    if (connection.createdAt == Connection::kUnset) {
      connection.createdAt = now;
    }

    switch (connection.state) {
      case Connection::State::Connecting:
        if (now - connection.createdAt >= m_config.connectTimeout) {
          dropped.emplace_back(peer, DisconnectReason::ConnectTimeout);
        } else if (now - connection.lastConnectSent >=
                   m_config.connectRetryInterval) {
          sendConnect(peer, connection.localSalt);
          connection.lastConnectSent = now;
        }
        break;

      case Connection::State::Accepting:
        // Asked but never followed up.
        if (now - connection.createdAt >= m_config.connectTimeout) {
          dropped.emplace_back(peer, DisconnectReason::ConnectTimeout);
        }
        break;

      case Connection::State::Connected:
        if (connection.timedOut(now)) {
          dropped.emplace_back(peer, DisconnectReason::Timeout);
          break;
        }
        m_outgoing.clear();
        connection.buildPackets(now, m_outgoing);
        for (const std::vector<uint8_t> &packet : m_outgoing) {
          // A full send buffer is just a lost packet; reliable data is resent.
          m_socket.sendTo(peer, packet.data(), packet.size());
        }
        break;
    }
  }
  for (const auto &drop : dropped) {
    removeConnection(drop.first, drop.second);
  }

  // Punching: a packet now and then until connected or time is up; peers ignore
  // it.
  for (size_t i = 0; i < m_punches.size();) {
    Punch &punch = m_punches[i];
    if (punch.until < 0.0) {
      punch.until = now + punch.duration;
      punch.nextSend = now;
    }
    if (now >= punch.until || isConnected(punch.to)) {
      m_punches.erase(m_punches.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    if (now >= punch.nextSend) {
      PacketWriter writer;
      writer.u32(m_config.protocolId);
      writer.u8(static_cast<uint8_t>(PacketType::Punch));
      m_socket.sendTo(punch.to, writer.data(), writer.size());
      punch.nextSend = now + kPunchInterval;
    }
    ++i;
  }
}

void Transport::handlePacket(const Address &from, const uint8_t *data,
                             size_t size, double now) {
  PacketReader reader(data, size);
  uint32_t protocolId = reader.u32();
  uint8_t type = reader.u8();
  if (reader.failed() || protocolId != m_config.protocolId) {
    return;
  }

  switch (static_cast<PacketType>(type)) {
    case PacketType::Connect: {
      uint32_t salt = reader.u32();
      if (!reader.failed() && size >= kConnectPacketSize) {
        handleConnect(from, salt, now);
      }
      break;
    }
    case PacketType::Accept: {
      uint32_t echoed = reader.u32();
      uint32_t salt = reader.u32();
      if (!reader.failed()) {
        handleAccept(from, echoed, salt, now);
      }
      break;
    }
    case PacketType::Reject: {
      uint32_t echoed = reader.u32();
      auto it = m_connections.find(from);
      if (!reader.failed() && it != m_connections.end() &&
          it->second->state == Connection::State::Connecting &&
          it->second->localSalt == echoed) {
        removeConnection(from, DisconnectReason::Rejected);
      }
      break;
    }
    case PacketType::Disconnect: {
      uint32_t id = reader.u32();
      auto it = m_connections.find(from);
      if (!reader.failed() && it != m_connections.end() &&
          it->second->remoteSalt != 0 && it->second->id() == id) {
        removeConnection(from, DisconnectReason::Remote);
      }
      break;
    }
    case PacketType::Data:
      handleData(from, size, reader, now);
      break;
    case PacketType::Punch:
      break;
  }
}

void Transport::handleConnect(const Address &from, uint32_t salt, double now) {
  auto it = m_connections.find(from);
  if (it != m_connections.end()) {
    Connection &connection = *it->second;
    if (connection.remoteSalt == 0) {
      // Crossed requests: take theirs and answer it; the other side does the
      // same, so one connection results.
      connection.remoteSalt = salt;
    }
    if (connection.remoteSalt == salt) {
      // Repeated request or lost answer: answer again.
      sendAccept(from, salt, connection.localSalt);
      return;
    }
    // A new salt means the peer started over. Ignore it while connecting out;
    // otherwise the old session is dead.
    if (connection.state == Connection::State::Connecting) {
      return;
    }
    removeConnection(from, DisconnectReason::Remote);
  }

  if (!m_acceptIncoming || m_connections.size() >= m_config.maxConnections) {
    sendReject(from, salt);
    return;
  }
  auto connection = std::make_unique<Connection>(
      from, Connection::State::Accepting, newSalt(), salt, m_config);
  connection->createdAt = now;
  sendAccept(from, salt, connection->localSalt);
  m_connections.emplace(from, std::move(connection));
}

void Transport::handleAccept(const Address &from, uint32_t echoedSalt,
                             uint32_t remoteSalt, double now) {
  auto it = m_connections.find(from);
  if (it == m_connections.end()) {
    return;
  }
  Connection &connection = *it->second;
  if (connection.state != Connection::State::Connecting ||
      echoedSalt != connection.localSalt || remoteSalt == 0 ||
      (connection.remoteSalt != 0 && connection.remoteSalt != remoteSalt)) {
    return;
  }
  connection.remoteSalt = remoteSalt;
  connection.markConnected(now);

  Event event;
  event.type = Event::Type::Connected;
  event.peer = from;
  m_events.push_back(std::move(event));
}

void Transport::handleData(const Address &from, size_t datagramSize,
                           PacketReader &reader, double now) {
  uint32_t id = reader.u32();
  auto it = m_connections.find(from);
  if (reader.failed() || it == m_connections.end()) {
    return;
  }
  Connection &connection = *it->second;
  // The salts give the id, so the peer's salt must be known first.
  if (connection.remoteSalt == 0 || connection.id() != id) {
    return;
  }

  if (connection.state != Connection::State::Connected) {
    // The right id proves the peer got our accept (or, if requests crossed,
    // sent its own).
    connection.markConnected(now);
    Event event;
    event.type = Event::Type::Connected;
    event.peer = from;
    m_events.push_back(std::move(event));
  }

  if (!connection.receiveData(reader, datagramSize, now, m_events)) {
    LOG_ERROR("Dropping %s: malformed packet", from.toString().c_str());
    sendDisconnect(from, connection.id());
    removeConnection(from, DisconnectReason::ProtocolError);
  }
}

void Transport::sendConnect(const Address &to, uint32_t salt) {
  PacketWriter writer;
  writer.u32(m_config.protocolId);
  writer.u8(static_cast<uint8_t>(PacketType::Connect));
  writer.u32(salt);
  while (writer.size() < kConnectPacketSize) {
    writer.u8(0);
  }
  m_socket.sendTo(to, writer.data(), writer.size());
}

void Transport::sendAccept(const Address &to, uint32_t echoedSalt,
                           uint32_t ownSalt) {
  PacketWriter writer;
  writer.u32(m_config.protocolId);
  writer.u8(static_cast<uint8_t>(PacketType::Accept));
  writer.u32(echoedSalt);
  writer.u32(ownSalt);
  m_socket.sendTo(to, writer.data(), writer.size());
}

void Transport::sendReject(const Address &to, uint32_t echoedSalt) {
  PacketWriter writer;
  writer.u32(m_config.protocolId);
  writer.u8(static_cast<uint8_t>(PacketType::Reject));
  writer.u32(echoedSalt);
  for (int i = 0; i < kDisconnectRepeats; ++i) {
    m_socket.sendTo(to, writer.data(), writer.size());
  }
}

void Transport::sendDisconnect(const Address &to, uint32_t connectionId) {
  PacketWriter writer;
  writer.u32(m_config.protocolId);
  writer.u8(static_cast<uint8_t>(PacketType::Disconnect));
  writer.u32(connectionId);
  for (int i = 0; i < kDisconnectRepeats; ++i) {
    m_socket.sendTo(to, writer.data(), writer.size());
  }
}

} // namespace componeng::net
