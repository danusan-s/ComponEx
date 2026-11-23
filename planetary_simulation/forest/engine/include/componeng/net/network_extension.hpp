#pragma once

#include "componeng/net/idatagram_socket.hpp"

#include <memory>

namespace componeng::net {

// An add-on that rides on a Network: it may wrap the socket the transport reads
// (to answer its own packets) and gets a tick every Network::update. NAT
// traversal is one; see nat/nat_traversal.hpp.
class INetworkExtension {
public:
  virtual ~INetworkExtension() = default;

  // Called by Network::open with the socket the transport will use. Return it,
  // or a decorator that owns it.
  virtual std::unique_ptr<IDatagramSocket>
  wrapSocket(std::unique_ptr<IDatagramSocket> socket) {
    return socket;
  }

  // Each Network::update, right after the transport's.
  virtual void update(double) {
  }

  // The network closed; drop anything that points into the socket.
  virtual void onClose() {
  }
};

} // namespace componeng::net
