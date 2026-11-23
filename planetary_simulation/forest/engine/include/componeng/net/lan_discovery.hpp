#pragma once

#include "componeng/net/address.hpp"
#include "componeng/net/idatagram_socket.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace componeng::net {

// Games may pick their own port if all their peers agree.
constexpr uint16_t kDefaultDiscoveryPort = 47777;
// Session names are cut to this many bytes.
constexpr size_t kMaxSessionNameLength = 32;
// Requests are padded to this size and no answer is bigger, so a forged request
// cannot make us send a victim more than it sent us.
constexpr size_t kDiscoverPacketSize = 128;

// Makes a discovery socket on `port` (0 = any); `shared` allows sharing the
// port. Tests supply their own; the default is real UDP.
using LanSocketFactory =
    std::function<std::unique_ptr<IDatagramSocket>(uint16_t port, bool shared)>;
LanSocketFactory defaultLanSocketFactory();

struct LanSessionInfo {
  uint32_t sessionId = 0;
  std::string name;
  Address address;
  uint16_t peers = 0;
  uint16_t maxPeers = 0;
  // `now` of the update that last heard from it.
  double lastSeen = 0.0;
};

// What a host tells searchers about its session.
struct LanAdvert {
  uint32_t sessionId = 0;
  std::string name;
  uint16_t gamePort = 0;
  uint16_t peers = 0;
  uint16_t maxPeers = 0;
};

// Answers searchers on the discovery port; never sends unasked. Requests for
// another protocolId are ignored, so different games do not see each other.
class LanAdvertiser {
public:
  LanAdvertiser() = default;

  // A busy port is retried by update(), so this never hard-fails.
  void start(uint32_t protocolId, uint16_t port,
             LanSocketFactory factory = LanSocketFactory());
  void stop();
  bool isRunning() const {
    return m_running;
  }
  bool isListening() const {
    return m_socket != nullptr;
  }
  void setAdvert(const LanAdvert &advert) {
    m_advert = advert;
  }
  void update(double now);

private:
  bool m_running = false;
  uint32_t m_protocolId = 0;
  uint16_t m_port = 0;
  LanSocketFactory m_factory;
  std::unique_ptr<IDatagramSocket> m_socket;
  LanAdvert m_advert;
  double m_nextOpenAttempt = -1.0;
};

// Asks once a second on the broadcast address and on this machine (so a game on
// the same PC is found), and keeps what answers.
class LanBrowser {
public:
  LanBrowser() = default;

  bool start(uint32_t protocolId, uint16_t port = kDefaultDiscoveryPort,
             LanSocketFactory factory = LanSocketFactory());
  void stop();
  bool isSearching() const {
    return m_socket != nullptr;
  }
  void update(double now);

  // Sessions heard from recently; a gone host drops out by itself.
  std::vector<LanSessionInfo> sessions() const;

  static constexpr double kSearchInterval = 1.0;
  static constexpr double kSessionExpiry = 3.5;

private:
  uint32_t m_protocolId = 0;
  uint16_t m_port = 0;
  std::unique_ptr<IDatagramSocket> m_socket;
  std::vector<LanSessionInfo> m_sessions;
  double m_now = 0.0;
  double m_lastRequest = -1e300;
};

} // namespace componeng::net
