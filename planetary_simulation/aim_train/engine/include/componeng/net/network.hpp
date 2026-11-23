#pragma once

#include "componeng/net/address.hpp"
#include "componeng/net/idatagram_socket.hpp"
#include "componeng/net/message_registry.hpp"
#include "componeng/net/network_extension.hpp"
#include "componeng/net/transport.hpp"
#include "componeng/utils/logger.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace componeng::net {

// What a game uses: open a port, connect to peers, send typed messages and
// receive them typed. Wraps the socket, Transport and MessageRegistry.
//
// Peers are equal: whoever connect()s and whoever accepts get the same kind of
// connection. For more than two players connect each to each (a mesh).
//
// Arrivals come as callbacks, run inside update(). NetPlugin's system
// calls update() each frame (see net_plugin.hpp); elsewhere call
// update(now) yourself. ECS events are an opt-in layer (ecs_bridge.hpp).
class Network {
public:
  Network();
  ~Network();

  Network(Network &&) noexcept;
  Network &operator=(Network &&) noexcept;
  Network(const Network &) = delete;
  Network &operator=(const Network &) = delete;

  // Opens a UDP socket on `port` (0 = any free) on every interface; closes any
  // previous one. The config is required: its protocolId must be set to a
  // number of your game's own, or open() fails.
  bool open(uint16_t port, const TransportConfig &config,
            Address::Family family = Address::Family::IPv4);
  // Uses a socket you made (e.g. a LoopbackSocket in tests).
  bool open(std::unique_ptr<IDatagramSocket> socket,
            const TransportConfig &config);
  void close();
  bool isOpen() const {
    return m_transport != nullptr;
  }
  const TransportConfig &config() const {
    return m_config;
  }
  // The IP is the wildcard: peers need your real one (NatTraversal can learn
  // it, see nat/nat_traversal.hpp).
  Address localAddress() const;

  void setAcceptIncoming(bool accept);
  // Starts connecting; the outcome arrives as onPeerConnected /
  // onConnectFailed. False if not open, or already connected or connecting. The
  // string overload takes a numeric "ip:port".
  bool connect(const Address &peer);
  bool connect(const std::string &text);
  void disconnect(const Address &peer);

  // Sends the peer packets for `duration` seconds so a router lets its own
  // packets in. Both sides do it at about the same time (hole punching).
  void punch(const Address &peerPublicAddress, double duration = 10.0);

  // Adds an extension; false if the network is open, because the socket is
  // wrapped when it opens. Add them before open().
  bool addExtension(std::unique_ptr<INetworkExtension> extension);
  // The extension of type T, or null.
  template <typename T> T *extension() {
    for (auto &candidate : m_extensions) {
      if (T *found = dynamic_cast<T *>(candidate.get())) {
        return found;
      }
    }
    return nullptr;
  }

  bool isConnected(const Address &peer) const;
  std::vector<Address> peers() const;
  ConnectionStats stats(const Address &peer) const;

  // Registers a message type on its channel (Reliable for what must arrive,
  // Unreliable for what goes stale). Every peer must register the same types;
  // unknown ones are counted in rejectedMessages().
  template <typename T>
  bool registerMessage(Channel channel = Channel::Reliable) {
    return m_registry.add<T>(channel);
  }
  template <typename T> bool onMessage(MessageRegistry::Handler<T> handler) {
    return m_registry.onReceive<T>(std::move(handler));
  }

  void onPeerConnected(std::function<void(const Address &)> handler);
  void onPeerDisconnected(
      std::function<void(const Address &, DisconnectReason)> handler);
  void onConnectFailed(
      std::function<void(const Address &, DisconnectReason)> handler);

  // Sends on the channel T was registered with. False if unregistered, not
  // open, not connected, or the peer is not keeping up.
  template <typename T> bool send(const Address &peer, const T &message) {
    if (!m_transport) {
      return false;
    }
    ByteWriter writer;
    Channel channel = Channel::Reliable;
    if (!m_registry.encode(message, writer, channel)) {
      LOG_ERROR("Cannot send '%s': it was never registered", T::kName);
      return false;
    }
    return m_transport->send(peer, channel, writer.data().data(),
                             writer.size());
  }

  // Returns how many peers it was queued for.
  template <typename T> size_t broadcast(const T &message) {
    if (!m_transport) {
      return 0;
    }
    ByteWriter writer;
    Channel channel = Channel::Reliable;
    if (!m_registry.encode(message, writer, channel)) {
      LOG_ERROR("Cannot send '%s': it was never registered", T::kName);
      return 0;
    }
    size_t queued = 0;
    for (const Address &peer : m_transport->connectedPeers()) {
      queued +=
          m_transport->send(peer, channel, writer.data().data(), writer.size());
    }
    return queued;
  }

  // `now` is seconds on one steady clock.
  void update(double now);

  // Dropped arrivals: unknown type, wrong channel or unparsable.
  uint64_t rejectedMessages() const {
    return m_rejectedMessages;
  }

private:
  void dispatch(const Event &event);

  MessageRegistry m_registry;
  std::vector<std::unique_ptr<INetworkExtension>> m_extensions;
  // The transport refers to the socket, so both live on the heap and survive a
  // move.
  std::unique_ptr<IDatagramSocket> m_socket;
  std::unique_ptr<Transport> m_transport;
  std::vector<std::function<void(const Address &)>> m_onConnected;
  std::vector<std::function<void(const Address &, DisconnectReason)>>
      m_onDisconnected;
  std::vector<std::function<void(const Address &, DisconnectReason)>>
      m_onConnectFailed;
  TransportConfig m_config;
  uint64_t m_rejectedMessages = 0;
};

} // namespace componeng::net
