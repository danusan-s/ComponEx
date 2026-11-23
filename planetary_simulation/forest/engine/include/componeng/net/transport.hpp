#pragma once

#include "componeng/net/address.hpp"
#include "componeng/net/idatagram_socket.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

namespace componeng::net {

class Connection;
class PacketReader;

// Unreliable: sent once, may be lost or reordered, must fit one packet.
// Reliable: delivered once and in order, any size up to maxMessageSize (split
// and reassembled). The channels are not ordered against each other.
enum class Channel : uint8_t { Unreliable, Reliable };

// Largest payload an unreliable message can have.
constexpr size_t kMaxUnreliableMessageSize = 1180;

enum class DisconnectReason : uint8_t {
  Local,         // this side called disconnect()
  Remote,        // the peer disconnected, or restarted
  Timeout,       // nothing heard from the peer for TransportConfig::timeout
  ProtocolError, // the peer sent something a well-behaved one never would
  Rejected,      // connect(): the peer refused (full, or not accepting)
  ConnectTimeout // connect(): the peer never answered
};

struct TransportConfig {
  // Packets with another value are ignored. Required (Network::open refuses 0):
  // set it per game and change it when your message formats change.
  uint32_t protocolId = 0;

  // Counts handshaking connections too; more are rejected.
  size_t maxConnections = 16;

  // Seconds between connection requests, and how long to keep trying.
  double connectRetryInterval = 0.25;
  double connectTimeout = 5.0;

  // No packet from the peer for this long drops the connection; idle ones send
  // keepalives.
  double timeout = 10.0;
  double keepAliveInterval = 1.0;

  // Largest reliable message, in bytes.
  size_t maxMessageSize = 1 << 20;
  // Unacked reliable bytes held per connection; send() fails beyond it (the
  // peer is not keeping up).
  size_t maxQueuedBytes = 4 << 20;
  // Packets per connection per update(). There is no congestion control; this
  // caps how fast a backlog goes out.
  size_t maxPacketsPerUpdate = 16;

  // Seeds connection ids (0 = OS random); fixed only for repeatable tests.
  uint32_t seed = 0;
};

struct Event {
  enum class Type : uint8_t {
    Connected,     // a handshake finished, whoever started it
    ConnectFailed, // a connect() never completed; see `reason`
    Disconnected,  // an established connection ended; see `reason`
    Message        // `data` arrived on `channel`
  };

  Type type = Type::Message;
  Address peer;
  DisconnectReason reason = DisconnectReason::Local;
  Channel channel = Channel::Reliable;
  std::vector<uint8_t> data;
};

struct ConnectionStats {
  // Smoothed round-trip time in seconds; 0 until measured.
  double rtt = 0.0;
  uint64_t packetsSent = 0;
  uint64_t packetsReceived = 0;
  uint64_t bytesSent = 0;
  uint64_t bytesReceived = 0;
  // Reliable entries sent again because their first packet was not acked.
  uint64_t reliableResends = 0;
};

// Connections, reliability and framing over one datagram socket, which must
// outlive it. No client/server: any Transport can connect() to any other, and
// two that connect to each other at once get one connection (what NAT hole
// punching needs). Peers are identified by Address.
//
// Driven by update(now) once a frame on the caller's thread; `now` is seconds
// on any steady clock, so tests can use simulated time.
class Transport {
public:
  explicit Transport(IDatagramSocket &socket,
                     const TransportConfig &config = TransportConfig());
  // Tells connected peers it is leaving.
  ~Transport();

  Transport(const Transport &) = delete;
  Transport &operator=(const Transport &) = delete;

  // Completion arrives as Event::Connected or ConnectFailed. False if `remote`
  // is invalid, already known, or the connection limit is reached.
  bool connect(const Address &remote);
  void disconnect(const Address &remote);
  // Sends small ignorable packets to `to` for `duration` seconds or until
  // connected, so this peer's NAT expects traffic from `to`. Use it on the side
  // being connected to; two peers that connect() to each other already punch.
  void punch(const Address &to, double duration = 10.0);
  void setAcceptIncoming(bool accept) {
    m_acceptIncoming = accept;
  }

  // Queues a message; it leaves on the next update(). False (nothing queued) if
  // not connected, too big for the channel, or the queue is full.
  bool send(const Address &peer, Channel channel, const void *data,
            size_t size);

  void update(double now);
  bool pollEvent(Event &event);

  bool isConnected(const Address &peer) const;
  std::vector<Address> connectedPeers() const;
  ConnectionStats stats(const Address &peer) const;

  const TransportConfig &config() const {
    return m_config;
  }

private:
  void handlePacket(const Address &from, const uint8_t *data, size_t size,
                    double now);
  void handleConnect(const Address &from, uint32_t salt, double now);
  void handleAccept(const Address &from, uint32_t echoedSalt,
                    uint32_t remoteSalt, double now);
  void handleData(const Address &from, size_t datagramSize,
                  PacketReader &reader, double now);

  // Queues Disconnected / ConnectFailed for connections the game knew of;
  // nothing for handshakes it never saw.
  void removeConnection(const Address &peer, DisconnectReason reason);

  void sendConnect(const Address &to, uint32_t salt);
  void sendAccept(const Address &to, uint32_t echoedSalt, uint32_t ownSalt);
  void sendReject(const Address &to, uint32_t echoedSalt);
  void sendDisconnect(const Address &to, uint32_t connectionId);
  uint32_t newSalt();

  IDatagramSocket &m_socket;
  TransportConfig m_config;
  bool m_acceptIncoming = false;
  std::mt19937 m_rng;
  std::unordered_map<Address, std::unique_ptr<Connection>> m_connections;
  std::deque<Event> m_events;
  std::vector<std::vector<uint8_t>> m_outgoing;

  struct Punch {
    Address to;
    double duration = 0.0;
    double until = -1.0;
    double nextSend = 0.0;
  };
  std::vector<Punch> m_punches;
};

} // namespace componeng::net
