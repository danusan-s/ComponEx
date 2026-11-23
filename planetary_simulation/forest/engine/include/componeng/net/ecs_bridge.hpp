#pragma once

#include "componeng/ecs/world.hpp"
#include "componeng/events/network_event.hpp"
#include "componeng/events/session_event.hpp"
#include "componeng/net/network.hpp"
#include "componeng/net/session.hpp"

#include <functional>
#include <memory>

namespace componeng::net {

// Turns network and session callbacks into ECS events, for games that prefer to
// read them in systems. Optional: the net core knows nothing about the ECS.
// NetPlugin creates one as a world resource; message events are opt
// in per type:
//
//   bridge.forward<MoveMessage>(session);  // SessionMessageEvent<MoveMessage>
//
// Events can be read for one frame, the one after the network system produced
// them.
class EcsBridge {
public:
  EcsBridge() : m_binding(std::make_shared<Binding>()) {
  }

  // Events are emitted only while a world is bound; NetPlugin does this.
  void bindWorld(ecs::World *world) {
    m_binding->world = world;
  }

  // PeerConnected / PeerDisconnected / ConnectFailed events.
  void attach(Network &network);
  // SessionJoined / JoinFailed / PeerJoined / PeerLeft / LeaderChanged events.
  void attach(Session &session);

  // MessageReceivedEvent<T> for each T the network receives. T must already be
  // registered on it.
  template <typename T> bool forward(Network &network) {
    std::shared_ptr<Binding> binding = m_binding;
    return network.onMessage<T>(
        [binding](const Address &from, const T &message) {
          if (binding->world) {
            binding->world->emitEvent(
                events::MessageReceivedEvent<T>{from, message});
          }
        });
  }

  // SessionMessageEvent<T> for each T received from a peer of the session.
  template <typename T> bool forward(Session &session) {
    std::shared_ptr<Binding> binding = m_binding;
    return session.onMessage<T>(std::function<void(PeerId, const T &)>(
        [binding](PeerId from, const T &message) {
          if (binding->world) {
            binding->world->emitEvent(
                events::SessionMessageEvent<T>{from, message});
          }
        }));
  }

private:
  // Shared with the callbacks, so they stay valid if the bridge is moved.
  struct Binding {
    ecs::World *world = nullptr;
  };
  std::shared_ptr<Binding> m_binding;
};

} // namespace componeng::net
