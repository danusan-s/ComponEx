#pragma once

#include "componeng/ecs/entity.hpp"
#include "componeng/ecs/system.hpp"
#include "componeng/net/nat/nat_traversal.hpp"
#include "componeng/net/object_sync.hpp"
#include "componeng/net/session.hpp"
#include "net_types.hpp"

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace multiplayer_demo {

// Hosts or joins a session and keeps one shared PlayerState per peer:
//  - this peer streams its own player (the object it owns),
//  - every other peer's object is drawn as a cuboid avatar.
class MultiplayerSystem : public componeng::ecs::ISystem {
public:
  void onCreate(const componeng::ecs::SystemState &state) override;
  void onUpdate(const componeng::ecs::SystemState &state) override;
  void onDestroy(const componeng::ecs::SystemState &state) override;

private:
  struct Avatar {
    componeng::ecs::EntityID body = 0;
    componeng::ecs::EntityID nose = 0; // small block marking the facing
    PlayerState shown;                 // smoothed towards the network state
  };

  void tryJoin(componeng::net::Session &session);
  void handleInviteCode(componeng::net::Session &session,
                        const std::string &text);
  void pollTerminal(componeng::net::Session &session);
  void updateRemote(componeng::ecs::World &world, float dt, double now);
  void placeAvatar(componeng::ecs::World &world, const Avatar &avatar);
  void drawOverlay(componeng::ecs::World &world);

  std::unique_ptr<componeng::net::SharedObjects<PlayerState>> m_players;
  componeng::net::ObjectId m_myObject = 0;
  bool m_created = false;       // our object has been add()ed
  double m_sinceSend = 0.0;     // throttles update() to kSendInterval
  std::unordered_map<componeng::net::ObjectId, Avatar> m_avatars;
  // When we first saw an object nobody owns; the leader removes stale ones
  // (a peer that left frees, but does not delete, what it owned).
  std::unordered_map<componeng::net::ObjectId, double> m_freeSince;
  std::string m_status = "starting";
  std::string m_joinAddress;    // where we are joining; empty when hosting
  int m_joinAttempts = 0;
  double m_joinTimeout = 15.0;

  // Internet play (--nat).
  struct Terminal { // lines typed into the terminal, read on a helper thread
    std::mutex mutex;
    std::deque<std::string> lines;
  };
  componeng::net::NatTraversal *m_nat = nullptr; // world resource; --nat only
  std::shared_ptr<Terminal> m_terminal;
  bool m_hosting = false;
  bool m_natDone = false;       // discovery finished (ok or not)
  bool m_natOk = false;
  bool m_printedCode = false;
  componeng::net::NatType m_natType = componeng::net::NatType::Unknown;
  std::string m_pendingCode;    // host's code given on the command line
  double m_retryAt = -1.0;      // world time of the next join attempt
};

} // namespace multiplayer_demo
