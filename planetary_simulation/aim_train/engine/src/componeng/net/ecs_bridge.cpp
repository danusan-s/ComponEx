#include "componeng/net/ecs_bridge.hpp"

namespace componeng::net {

void EcsBridge::attach(Network &network) {
  std::shared_ptr<Binding> binding = m_binding;
  network.onPeerConnected([binding](const Address &peer) {
    if (binding->world) {
      binding->world->emitEvent(events::PeerConnectedEvent{peer});
    }
  });
  network.onPeerDisconnected([binding](const Address &peer,
                                       DisconnectReason reason) {
    if (binding->world) {
      binding->world->emitEvent(events::PeerDisconnectedEvent{peer, reason});
    }
  });
  network.onConnectFailed(
      [binding](const Address &peer, DisconnectReason reason) {
        if (binding->world) {
          binding->world->emitEvent(events::ConnectFailedEvent{peer, reason});
        }
      });
}

void EcsBridge::attach(Session &session) {
  std::shared_ptr<Binding> binding = m_binding;
  session.onJoined([binding] {
    if (binding->world) {
      binding->world->emitEvent(events::SessionJoinedEvent{});
    }
  });
  session.onJoinFailed([binding](JoinFailure reason) {
    if (binding->world) {
      binding->world->emitEvent(events::SessionJoinFailedEvent{reason});
    }
  });
  session.onPeerJoined([binding](PeerId peer) {
    if (binding->world) {
      binding->world->emitEvent(events::SessionPeerJoinedEvent{peer});
    }
  });
  session.onPeerLeft([binding](PeerId peer, DisconnectReason reason) {
    if (binding->world) {
      binding->world->emitEvent(events::SessionPeerLeftEvent{peer, reason});
    }
  });
  session.onLeaderChanged([binding](PeerId leader) {
    if (binding->world) {
      binding->world->emitEvent(events::SessionLeaderChangedEvent{leader});
    }
  });
}

} // namespace componeng::net
