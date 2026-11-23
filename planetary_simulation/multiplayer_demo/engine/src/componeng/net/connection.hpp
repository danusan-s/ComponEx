#pragma once

// Private: one peer's state after the handshake (Transport owns the handshake
// and routing): sequencing and acks, reliable resend, fragmentation, keepalive
// and timeout.

#include "componeng/net/address.hpp"
#include "componeng/net/transport.hpp"
#include "packet_io.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace componeng::net {

class Connection {
public:
  // Connecting: request sent, awaiting the accept. Accepting: answered a peer's
  // request; becomes Connected on the first packet proving the answer arrived.
  enum class State : uint8_t { Connecting, Accepting, Connected };

  static constexpr double kUnset = -1e300;

  Connection(const Address &remote, State state, uint32_t localSalt,
             uint32_t remoteSalt, const TransportConfig &config);

  State state;
  const uint32_t localSalt;
  // 0 until the peer's salt is known.
  uint32_t remoteSalt;
  double createdAt = kUnset;
  double lastConnectSent = kUnset;

  const Address &remote() const {
    return m_remote;
  }
  // Both sides derive the same id; Data packets carry it to keep out strays.
  uint32_t id() const {
    return localSalt ^ remoteSalt;
  }

  void markConnected(double now);

  // Queues a message, fragmenting a big reliable one. False if it does not fit
  // the channel or the queue is full.
  bool queue(Channel channel, const void *data, size_t size);

  // The connection id is already checked. False on a malformed packet or
  // protocol violation: drop the connection.
  bool receiveData(PacketReader &reader, size_t datagramSize, double now,
                   std::deque<Event> &events);

  void buildPackets(double now, std::vector<std::vector<uint8_t>> &out);

  bool timedOut(double now) const {
    return now - m_lastReceive >= m_config.timeout;
  }
  ConnectionStats stats() const;

private:
  struct SentPacket {
    double time = 0.0;
    bool acked = false;
    std::vector<uint16_t> reliableIds;
  };

  struct Outgoing {
    uint16_t id = 0;
    bool more = false;
    bool everSent = false;
    bool acked = false;
    double lastSent = 0.0;
    std::vector<uint8_t> data;
  };

  struct Incoming {
    bool more = false;
    std::vector<uint8_t> data;
  };

  bool registerReceived(uint16_t sequence);
  void processAcks(uint16_t ack, uint32_t ackBits, double now);
  void ackPacket(uint16_t sequence, double now);
  void ackReliable(uint16_t id);
  double resendTimeout() const;

  bool receiveReliable(uint16_t id, bool more, std::vector<uint8_t> &&data,
                       std::deque<Event> &events);
  bool deliverFragment(bool more, std::vector<uint8_t> &data,
                       std::deque<Event> &events);

  Address m_remote;
  TransportConfig m_config;

  // m_sent holds recent packets, oldest first and contiguous, so a sequence
  // maps to an index.
  uint16_t m_sendSequence = 0;
  uint16_t m_sentFrontSequence = 0;
  std::deque<SentPacket> m_sent;
  std::deque<Outgoing> m_reliableOut;
  uint16_t m_nextReliableId = 0;
  size_t m_queuedBytes = 0;
  std::deque<std::vector<uint8_t>> m_unreliableOut;

  // Highest sequence seen plus one bit for each of the 32 before it (bit 0 =
  // highest - 1): what acks are made from.
  bool m_haveReceived = false;
  uint16_t m_receivedHighest = 0xFFFF;
  uint32_t m_receivedBits = 0;
  bool m_ackPending = false;
  uint16_t m_expectedReliableId = 0;
  std::map<uint16_t, Incoming> m_reliableIn;
  std::vector<uint8_t> m_assembly;

  double m_lastReceive = 0.0;
  double m_lastSend = kUnset;
  double m_smoothedRtt = 0.0;
  bool m_haveRtt = false;
  ConnectionStats m_stats;
};

} // namespace componeng::net
