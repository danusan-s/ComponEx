#include "connection.hpp"

#include "componeng/utils/logger.hpp"

#include <algorithm>
#include <utility>

namespace componeng::net {

namespace {

// Reliable entries in flight; bounds what the receiver may hold out of order.
constexpr size_t kReliableWindow = 256;
// Under half the id space, so an id identifies one entry.
constexpr size_t kMaxQueuedReliableEntries = 16384;
constexpr size_t kMaxQueuedUnreliable = 256;
// An ack covers at most 33 packets.
constexpr size_t kSentHistory = 256;
// Senders stay within kReliableWindow, so more than this is a broken peer.
constexpr uint16_t kReliableReceiveWindow = 512;

constexpr double kInitialRto = 0.25;
constexpr double kMinRto = 0.05;
constexpr double kMaxRto = 1.0;

static_assert(kMaxUnreliableMessageSize ==
                  kMaxPacketSize - kDataHeaderSize - kUnreliableEntryOverhead,
              "kMaxUnreliableMessageSize must match the wire layout");

void writeEntry(PacketWriter &writer, bool reliable, bool more, uint16_t id,
                const std::vector<uint8_t> &data) {
  writer.u8(static_cast<uint8_t>((reliable ? kEntryReliable : 0) |
                                 (more ? kEntryMore : 0)));
  if (reliable) {
    writer.u16(id);
  }
  writer.u16(static_cast<uint16_t>(data.size()));
  writer.bytes(data.data(), data.size());
}

} // namespace

Connection::Connection(const Address &remote, State initialState,
                       uint32_t ownSalt, uint32_t peerSalt,
                       const TransportConfig &config)
    : state(initialState), localSalt(ownSalt), remoteSalt(peerSalt),
      m_remote(remote), m_config(config) {
}

void Connection::markConnected(double now) {
  state = State::Connected;
  m_lastReceive = now;
  // Unset: the first buildPackets() sends at once, telling an Accepting peer
  // its answer arrived.
  m_lastSend = kUnset;
}

double Connection::resendTimeout() const {
  return m_haveRtt ? std::clamp(m_smoothedRtt * 2.0, kMinRto, kMaxRto)
                   : kInitialRto;
}

ConnectionStats Connection::stats() const {
  ConnectionStats result = m_stats;
  result.rtt = m_haveRtt ? m_smoothedRtt : 0.0;
  return result;
}

bool Connection::queue(Channel channel, const void *data, size_t size) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);

  if (channel == Channel::Unreliable) {
    if (size > kMaxUnreliableMessageSize ||
        m_unreliableOut.size() >= kMaxQueuedUnreliable) {
      return false;
    }
    m_unreliableOut.emplace_back(bytes, bytes + size);
    return true;
  }

  size_t fragments = std::max<size_t>(1, (size + kMaxReliableFragment - 1) /
                                             kMaxReliableFragment);
  if (size > m_config.maxMessageSize ||
      m_queuedBytes + size > m_config.maxQueuedBytes ||
      m_reliableOut.size() + fragments > kMaxQueuedReliableEntries) {
    return false;
  }
  // Consecutive ids, `more` on all but the last: ordered delivery is enough to
  // reassemble.
  size_t offset = 0;
  do {
    size_t chunk = std::min(size - offset, kMaxReliableFragment);
    Outgoing entry;
    entry.id = m_nextReliableId++;
    entry.more = offset + chunk < size;
    entry.data.assign(bytes + offset, bytes + offset + chunk);
    m_reliableOut.push_back(std::move(entry));
    offset += chunk;
  } while (offset < size);
  m_queuedBytes += size;
  return true;
}

void Connection::buildPackets(double now,
                              std::vector<std::vector<uint8_t>> &out) {
  const double rto = resendTimeout();
  size_t built = 0;

  while (built < m_config.maxPacketsPerUpdate) {
    PacketWriter writer;
    writer.u32(m_config.protocolId);
    writer.u8(static_cast<uint8_t>(PacketType::Data));
    writer.u32(id());
    writer.u16(m_sendSequence);
    writer.u16(m_receivedHighest);
    writer.u32(m_receivedBits);

    SentPacket record;
    record.time = now;
    size_t entries = 0;

    // Never sent, or last sent longer ago than the resend timeout.
    size_t limit = std::min(kReliableWindow, m_reliableOut.size());
    for (size_t i = 0; i < limit; ++i) {
      Outgoing &entry = m_reliableOut[i];
      if (entry.acked) {
        continue;
      }
      if (entry.everSent && now - entry.lastSent < rto) {
        continue;
      }
      if (writer.remaining() < kReliableEntryOverhead + entry.data.size()) {
        break;
      }
      writeEntry(writer, true, entry.more, entry.id, entry.data);
      if (entry.everSent) {
        ++m_stats.reliableResends;
      }
      entry.everSent = true;
      entry.lastSent = now;
      record.reliableIds.push_back(entry.id);
      ++entries;
    }

    while (!m_unreliableOut.empty()) {
      const std::vector<uint8_t> &message = m_unreliableOut.front();
      if (writer.remaining() < kUnreliableEntryOverhead + message.size()) {
        break;
      }
      writeEntry(writer, false, false, 0, message);
      m_unreliableOut.pop_front();
      ++entries;
    }

    // A bare packet only carries an owed ack or a keepalive, and only as the
    // first of an update.
    bool bare = built == 0 && (m_ackPending ||
                               now - m_lastSend >= m_config.keepAliveInterval);
    if (entries == 0 && !bare) {
      break;
    }
    if (writer.overflowed()) {
      LOG_ERROR("Packet to %s overflowed; dropped",
                m_remote.toString().c_str());
      break;
    }

    m_sent.push_back(std::move(record));
    ++m_sendSequence;
    while (m_sent.size() > kSentHistory) {
      m_sent.pop_front();
      ++m_sentFrontSequence;
    }
    m_ackPending = false;
    m_lastSend = now;
    ++m_stats.packetsSent;
    m_stats.bytesSent += writer.size();
    out.emplace_back(writer.data(), writer.data() + writer.size());
    ++built;
  }
}

bool Connection::registerReceived(uint16_t sequence) {
  if (!m_haveReceived) {
    m_haveReceived = true;
    m_receivedHighest = sequence;
    m_receivedBits = 0;
    return true;
  }
  // Sequences wrap, so "newer" means within half the range ahead.
  uint16_t ahead = static_cast<uint16_t>(sequence - m_receivedHighest);
  if (ahead == 0) {
    return false;
  }
  if (ahead < 32768) {
    // The old highest becomes bit (ahead - 1); the rest slide up.
    m_receivedBits = ahead < 32 ? m_receivedBits << ahead : 0;
    if (ahead <= 32) {
      m_receivedBits |= 1u << (ahead - 1);
    }
    m_receivedHighest = sequence;
    return true;
  }
  uint16_t behind = static_cast<uint16_t>(m_receivedHighest - sequence);
  // Too old to tell duplicates apart; safe to drop (reliable entries are
  // resent).
  if (behind > 32) {
    return false;
  }
  uint32_t bit = 1u << (behind - 1);
  if (m_receivedBits & bit) {
    return false;
  }
  m_receivedBits |= bit;
  return true;
}

void Connection::processAcks(uint16_t ack, uint32_t ackBits, double now) {
  ackPacket(ack, now);
  for (uint16_t i = 0; i < 32; ++i) {
    if (ackBits & (1u << i)) {
      ackPacket(static_cast<uint16_t>(ack - 1 - i), now);
    }
  }
}

void Connection::ackPacket(uint16_t sequence, double now) {
  // Anything not in the history (older, or never sent) lands out of range.
  uint16_t index = static_cast<uint16_t>(sequence - m_sentFrontSequence);
  if (index >= m_sent.size()) {
    return;
  }
  SentPacket &packet = m_sent[index];
  if (packet.acked) {
    return;
  }
  packet.acked = true;

  double sample = std::max(0.0, now - packet.time);
  m_smoothedRtt = m_haveRtt ? m_smoothedRtt * 0.875 + sample * 0.125 : sample;
  m_haveRtt = true;

  for (uint16_t id : packet.reliableIds) {
    ackReliable(id);
  }
}

void Connection::ackReliable(uint16_t id) {
  if (m_reliableOut.empty()) {
    return;
  }
  uint16_t index = static_cast<uint16_t>(id - m_reliableOut.front().id);
  if (index >= m_reliableOut.size()) {
    return;
  }
  Outgoing &entry = m_reliableOut[index];
  if (entry.acked) {
    return;
  }
  entry.acked = true;
  m_queuedBytes -= entry.data.size();
  while (!m_reliableOut.empty() && m_reliableOut.front().acked) {
    m_reliableOut.pop_front();
  }
}

bool Connection::receiveData(PacketReader &reader, size_t datagramSize,
                             double now, std::deque<Event> &events) {
  uint16_t sequence = reader.u16();
  uint16_t ack = reader.u16();
  uint32_t ackBits = reader.u32();
  if (reader.failed()) {
    return false;
  }

  m_lastReceive = now;
  ++m_stats.packetsReceived;
  m_stats.bytesReceived += datagramSize;
  processAcks(ack, ackBits, now);

  // Duplicate or too-late packet: its acks are still read; its messages were
  // handled or will be resent.
  if (!registerReceived(sequence)) {
    return true;
  }

  size_t entries = 0;
  while (reader.remaining() > 0) {
    uint8_t flags = reader.u8();
    bool reliable = (flags & kEntryReliable) != 0;
    bool more = (flags & kEntryMore) != 0;
    uint16_t id = reliable ? reader.u16() : 0;
    uint16_t length = reader.u16();
    const uint8_t *payload = reader.bytes(length);
    if (reader.failed()) {
      return false;
    }
    ++entries;

    std::vector<uint8_t> data(payload, payload + length);
    if (reliable) {
      if (!receiveReliable(id, more, std::move(data), events)) {
        return false;
      }
    } else {
      Event event;
      event.type = Event::Type::Message;
      event.peer = m_remote;
      event.channel = Channel::Unreliable;
      event.data = std::move(data);
      events.push_back(std::move(event));
    }
  }
  // Bare packets are not acked, or idle peers would trade acks forever.
  if (entries > 0) {
    m_ackPending = true;
  }
  return true;
}

bool Connection::receiveReliable(uint16_t id, bool more,
                                 std::vector<uint8_t> &&data,
                                 std::deque<Event> &events) {
  uint16_t ahead = static_cast<uint16_t>(id - m_expectedReliableId);
  // Behind the expected id: an already-delivered resend.
  if (ahead >= 32768) {
    return true;
  }
  if (ahead >= kReliableReceiveWindow) {
    LOG_ERROR("Reliable id %u is %u ahead of %u, from %s", id, ahead,
              m_expectedReliableId, m_remote.toString().c_str());
    return false;
  }
  if (ahead > 0) {
    // Early: keep it, the sender will not repeat what it saw acked.
    m_reliableIn.emplace(id, Incoming{more, std::move(data)});
    return true;
  }

  if (!deliverFragment(more, data, events)) {
    return false;
  }
  ++m_expectedReliableId;
  for (auto it = m_reliableIn.find(m_expectedReliableId);
       it != m_reliableIn.end(); it = m_reliableIn.find(m_expectedReliableId)) {
    bool ok = deliverFragment(it->second.more, it->second.data, events);
    m_reliableIn.erase(it);
    if (!ok) {
      return false;
    }
    ++m_expectedReliableId;
  }
  return true;
}

bool Connection::deliverFragment(bool more, std::vector<uint8_t> &data,
                                 std::deque<Event> &events) {
  if (m_assembly.size() + data.size() > m_config.maxMessageSize) {
    LOG_ERROR("Reliable message from %s exceeds %zu bytes",
              m_remote.toString().c_str(), m_config.maxMessageSize);
    return false;
  }
  m_assembly.insert(m_assembly.end(), data.begin(), data.end());
  if (more) {
    return true;
  }
  Event event;
  event.type = Event::Type::Message;
  event.peer = m_remote;
  event.channel = Channel::Reliable;
  event.data = std::move(m_assembly);
  m_assembly.clear();
  events.push_back(std::move(event));
  return true;
}

} // namespace componeng::net
