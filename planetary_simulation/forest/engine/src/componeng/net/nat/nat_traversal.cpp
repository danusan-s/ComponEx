#include "componeng/net/nat/nat_traversal.hpp"

#include "componeng/net/nat/invite_code.hpp"
#include "componeng/net/nat/stun_socket.hpp"
#include "componeng/utils/logger.hpp"

#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace componeng::net {
namespace {

// What actually rides on the network: the StunSocket and the discovery
// bookkeeping.
class NatExtension final : public INetworkExtension {
public:
  std::unique_ptr<IDatagramSocket>
  wrapSocket(std::unique_ptr<IDatagramSocket> socket) override {
    auto stunSocket = std::make_unique<StunSocket>(std::move(socket));
    m_stun = stunSocket.get();
    return stunSocket;
  }

  void onClose() override {
    m_stun = nullptr;
    m_resolve.reset();
    m_pending = false;
  }

  void update(double now) override {
    if (!m_stun) {
      return;
    }
    if (m_resolve) {
      std::vector<Address> servers;
      bool ready = false;
      {
        std::lock_guard<std::mutex> lock(m_resolve->mutex);
        if (m_resolve->done) {
          servers = std::move(m_resolve->servers);
          ready = true;
        }
      }
      if (ready) {
        m_resolve.reset();
        m_stun->discovery().start(servers);
      }
    }
    m_stun->update(now);

    if (m_pending && !m_resolve) {
      NatDiscovery::State state = m_stun->discovery().state();
      if (state == NatDiscovery::State::Done ||
          state == NatDiscovery::State::Failed) {
        m_pending = false;
        bool ok = state == NatDiscovery::State::Done;
        NatInfo info = m_stun->discovery().info();
        for (size_t i = 0; i < m_handlers.size(); ++i) {
          m_handlers[i](ok, info);
        }
      }
    }
  }

  void discover(std::vector<std::string> servers, Address::Family family) {
    if (!m_stun) {
      return;
    }
    if (servers.empty()) {
      servers = NatTraversal::defaultStunServers();
    }
    m_pending = true;
    m_resolve.reset();

    // Numeric addresses need no lookup; only names do.
    std::vector<Address> numeric;
    std::vector<std::string> names;
    for (const std::string &text : servers) {
      Address address;
      if (Address::parse(text, address)) {
        if (address.family() == family) {
          numeric.push_back(address);
        }
      } else {
        names.push_back(text);
      }
    }
    if (names.empty()) {
      m_stun->discovery().start(numeric);
      return;
    }

    // Detached and touching only what it shares, so close() never waits for a
    // slow lookup.
    m_resolve = std::make_shared<ResolveJob>();
    m_resolve->servers = numeric;
    std::thread([job = m_resolve, names, family] {
      std::vector<Address> found;
      for (const std::string &text : names) {
        size_t colon = text.rfind(':');
        if (colon == std::string::npos || colon == 0) {
          continue;
        }
        uint32_t port = 0;
        try {
          port = static_cast<uint32_t>(std::stoul(text.substr(colon + 1)));
        } catch (...) {
          continue;
        }
        Address address;
        if (port != 0 && port <= 65535 &&
            Address::resolve(text.substr(0, colon), static_cast<uint16_t>(port),
                             family, address)) {
          found.push_back(address);
        }
      }
      std::lock_guard<std::mutex> lock(job->mutex);
      job->servers.insert(job->servers.end(), found.begin(), found.end());
      job->done = true;
    }).detach();
  }

  void addHandler(std::function<void(bool, const NatInfo &)> handler) {
    m_handlers.push_back(std::move(handler));
  }

  NatDiscovery::State state() const {
    return m_stun ? m_stun->discovery().state() : NatDiscovery::State::Idle;
  }

  NatInfo info() const {
    return m_stun ? m_stun->discovery().info() : NatInfo();
  }

private:
  // Where the lookup thread leaves its answer (lookups can take seconds).
  struct ResolveJob {
    std::mutex mutex;
    bool done = false;
    std::vector<Address> servers;
  };

  // The socket the network owns, seen as a StunSocket; null when closed.
  StunSocket *m_stun = nullptr;
  std::shared_ptr<ResolveJob> m_resolve;
  bool m_pending = false;
  std::vector<std::function<void(bool, const NatInfo &)>> m_handlers;
};

} // namespace

NatTraversal::NatTraversal(Network &network) : m_network(network) {
  if (!m_network.extension<NatExtension>() &&
      !m_network.addExtension(std::make_unique<NatExtension>())) {
    LOG_ERROR("NatTraversal must be created before the Network opens");
  }
}

bool NatTraversal::isActive() const {
  return m_network.extension<NatExtension>() != nullptr;
}

void NatTraversal::discoverPublicAddress(std::vector<std::string> servers) {
  if (NatExtension *ext = m_network.extension<NatExtension>()) {
    ext->discover(std::move(servers), m_network.localAddress().family());
  }
}

NatDiscovery::State NatTraversal::discoveryState() const {
  NatExtension *ext = m_network.extension<NatExtension>();
  return ext ? ext->state() : NatDiscovery::State::Idle;
}

Address NatTraversal::publicAddress() const {
  NatExtension *ext = m_network.extension<NatExtension>();
  return ext ? ext->info().publicAddress : Address();
}

NatType NatTraversal::natType() const {
  NatExtension *ext = m_network.extension<NatExtension>();
  return ext ? ext->info().type : NatType::Unknown;
}

void NatTraversal::onDiscovery(
    std::function<void(bool, const NatInfo &)> handler) {
  if (NatExtension *ext = m_network.extension<NatExtension>()) {
    ext->addHandler(std::move(handler));
  }
}

std::string NatTraversal::inviteCode() const {
  return encodeInvite(publicAddress());
}

bool NatTraversal::connectToInvite(const std::string &code) {
  Address peer;
  return decodeInvite(code, peer) && m_network.connect(peer);
}

std::vector<std::string> NatTraversal::defaultStunServers() {
  return {"stun.l.google.com:19302", "stun.cloudflare.com:3478"};
}

} // namespace componeng::net
