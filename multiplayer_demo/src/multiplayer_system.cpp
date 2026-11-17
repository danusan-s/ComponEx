#include "multiplayer_system.hpp"

#include "local_player.hpp"

#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/net/network.hpp"
#include "componeng/net/session.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/utils/logger.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>

namespace multiplayer_demo {

using namespace componeng;

namespace {
constexpr double kSendInterval = 1.0 / 30.0; // seconds between state updates
constexpr double kOrphanGrace = 3.0;         // seconds before a free object is removed
constexpr int kMaxJoinAttempts = 5;
constexpr float kSmoothing = 18.0f;          // avatar follow speed (1/s)

const core::Vec3 kPalette[] = {
    {0.90f, 0.25f, 0.25f}, {0.25f, 0.55f, 0.95f}, {0.30f, 0.80f, 0.35f},
    {0.95f, 0.75f, 0.20f}, {0.75f, 0.35f, 0.90f}, {0.20f, 0.85f, 0.85f},
};

core::Vec3 colorFor(net::PeerId peer) {
  return kPalette[peer % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

// Shortest signed turn from a to b, in degrees.
float angleDelta(float a, float b) {
  float d = std::fmod(b - a, 360.0f);
  if (d > 180.0f)
    d -= 360.0f;
  if (d < -180.0f)
    d += 360.0f;
  return d;
}

ecs::EntityID spawnBlock(ecs::World &world, const core::Vec3 &color) {
  ecs::EntityID e = world.createEntity();
  world.addComponents(
      e, renderer::MeshComponent{.meshName = "cube"},
      core::TransformComponent{},
      renderer::MaterialComponent{.materialName = "default_diffuse"},
      renderer::ColorComponent{.color = core::Vec4(color, 1.0f)});
  return e;
}
} // namespace

void MultiplayerSystem::onCreate(const ecs::SystemState &state) {
  ecs::World &world = *state.world;
  auto &network = world.getResource<net::Network>();
  auto &session = world.getResource<net::Session>();
  const auto &launch = world.getResource<LaunchConfig>();

  // One shared set holding every player; registers its messages on the session.
  m_players = std::make_unique<net::SharedObjects<PlayerState>>(
      session, "demo.players");
  m_players->onClaimDenied([](net::ObjectId, net::PeerId owner) {
    LOG_ERROR("Our player object is already owned by peer %u", unsigned(owner));
  });

  session.onJoined([&session] {
    LOG_INFO("Joined the session as peer %u", unsigned(session.self()));
  });
  session.onJoinFailed([this, &world](net::JoinFailure why) {
    LOG_ERROR("Could not join the session (reason %d)", int(why));
    m_status = "join failed (reason " + std::to_string(int(why)) + ")";
    if (m_joinAttempts < kMaxJoinAttempts) {
      m_retryAt = world.time + 1.0; // try again shortly; the host may be slow
    }
  });
  session.onPeerJoined(
      [](net::PeerId id) { LOG_INFO("Peer %u joined", unsigned(id)); });
  session.onPeerLeft([](net::PeerId id, net::DisconnectReason) {
    LOG_INFO("Peer %u left", unsigned(id));
  });

  net::TransportConfig config;
  config.protocolId = kProtocolId;
  bool host = launch.host;
  m_joinAddress = launch.address;
  if (!network.open(launch.port, config)) {
    if (host && launch.autoJoinLocal) {
      // Someone on this machine already hosts that port: join them instead.
      LOG_INFO("UDP port %u is taken; joining the instance hosting it",
               unsigned(launch.port));
      host = false;
      m_joinAddress = "127.0.0.1:" + std::to_string(launch.port);
      if (!network.open(0, config)) {
        m_status = "cannot open a UDP port";
        return;
      }
    } else {
      LOG_ERROR("Cannot open UDP port %u (is another instance already "
                "hosting?)",
                unsigned(launch.port));
      m_status = "cannot open port " + std::to_string(launch.port);
      return;
    }
  }

  if (host) {
    m_joinAddress.clear();
    net::SessionConfig sc;
    sc.name = "ComponEx multiplayer demo";
    session.host(sc);
    LOG_INFO("Hosting on UDP port %u; join with: MultiplayerDemo <this-ip>:%u",
             unsigned(network.localAddress().port()),
             unsigned(network.localAddress().port()));
  } else {
    tryJoin(session);
  }
}

void MultiplayerSystem::tryJoin(net::Session &session) {
  ++m_joinAttempts;
  m_retryAt = -1.0;
  if (!session.join(m_joinAddress)) {
    LOG_ERROR("Cannot join '%s' (expected a numeric ip:port)",
              m_joinAddress.c_str());
    m_status = "bad address '" + m_joinAddress + "'";
    m_joinAttempts = kMaxJoinAttempts; // retrying cannot help
  } else {
    LOG_INFO("Joining %s (attempt %d) ...", m_joinAddress.c_str(),
             m_joinAttempts);
    m_status = "joining " + m_joinAddress + " ...";
  }
}

void MultiplayerSystem::onUpdate(const ecs::SystemState &state) {
  ecs::World &world = *state.world;
  auto &session = world.getResource<net::Session>();

  if (m_retryAt >= 0.0 && world.time >= m_retryAt &&
      session.state() == net::Session::State::Idle) {
    tryJoin(session);
  }

  drawOverlay(world);
  if (!m_players || !session.isActive() || !m_players->synced()) {
    return;
  }

  // Our own player: add it once, ask for ownership, then stream it.
  if (!m_created) {
    m_myObject = m_players->newId();
    PlayerState initial;
    m_players->add(m_myObject, initial);
    m_players->claim(m_myObject);
    m_created = true;
  }

  if (m_players->ownedByMe(m_myObject)) {
    m_sinceSend += state.deltaTime;
    if (m_sinceSend >= kSendInterval) {
      m_sinceSend = 0.0;
      PlayerState now;
      world.query()
          .read<core::TransformComponent, LocalPlayerComponent>()
          .each([&](const core::TransformComponent &t,
                    const LocalPlayerComponent &look) {
            now.position = t.position;
            now.yaw = look.yaw;
            now.pitch = look.pitch;
          });
      m_players->update(m_myObject, now); // unreliable: the next one replaces it
    }
  }

  updateRemote(world, state.deltaTime, world.time);
}

void MultiplayerSystem::updateRemote(ecs::World &world, float dt, double now) {
  auto &session = world.getResource<net::Session>();
  const float follow = 1.0f - std::exp(-kSmoothing * dt);

  for (net::ObjectId id : m_players->ids()) {
    if (id == m_myObject || m_players->ownedByMe(id)) {
      continue;
    }
    const net::PeerId owner = m_players->ownerOf(id);

    if (owner == net::kNoPeer) {
      // Not owned: its player left (or has not claimed it yet). Hide it, and
      // let the leader delete it if it stays that way.
      if (auto it = m_avatars.find(id); it != m_avatars.end()) {
        world.destroyEntity(it->second.body);
        world.destroyEntity(it->second.nose);
        m_avatars.erase(it);
      }
      double &since = m_freeSince.try_emplace(id, now).first->second;
      if (session.isLeader() && now - since > kOrphanGrace) {
        m_players->remove(id);
        m_freeSince.erase(id);
      }
      continue;
    }
    m_freeSince.erase(id);

    const PlayerState *target = m_players->get(id);
    if (!target) {
      continue;
    }

    auto it = m_avatars.find(id);
    if (it == m_avatars.end()) {
      Avatar avatar;
      const core::Vec3 color = colorFor(owner);
      avatar.body = spawnBlock(world, color);
      avatar.nose = spawnBlock(world, core::Vec3(1.0f));
      avatar.shown = *target; // appear in place, do not fly in from the origin
      it = m_avatars.emplace(id, avatar).first;
      LOG_INFO("Showing player of peer %u", unsigned(owner));
    }

    Avatar &avatar = it->second;
    avatar.shown.position += (target->position - avatar.shown.position) * follow;
    avatar.shown.yaw += angleDelta(avatar.shown.yaw, target->yaw) * follow;
    avatar.shown.pitch = target->pitch;
    placeAvatar(world, avatar);
  }

  // Objects that were removed entirely.
  for (auto it = m_avatars.begin(); it != m_avatars.end();) {
    if (!m_players->contains(it->first)) {
      world.destroyEntity(it->second.body);
      world.destroyEntity(it->second.nose);
      it = m_avatars.erase(it);
    } else {
      ++it;
    }
  }
}

void MultiplayerSystem::placeAvatar(ecs::World &world, const Avatar &avatar) {
  const float yaw = core::radians(avatar.shown.yaw);
  // The cube's local +X is "forward": rotating by -yaw about Y points it along
  // (cos yaw, 0, sin yaw), the direction the camera looks at that yaw.
  const core::Vec3 rotation(0.0f, -avatar.shown.yaw, 0.0f);
  const core::Vec3 forward(std::cos(yaw), 0.0f, std::sin(yaw));

  auto &body = world.getComponent<core::TransformComponent>(avatar.body);
  body.position = avatar.shown.position;
  body.rotation = rotation;
  body.scale = core::Vec3(kBodyDepth, kBodyHeight, kBodyWidth);

  auto &nose = world.getComponent<core::TransformComponent>(avatar.nose);
  nose.position = avatar.shown.position + forward * (kBodyDepth * 0.5f + 0.1f) +
                  core::Vec3(0.0f, kEyeHeight - 0.1f, 0.0f);
  nose.rotation = rotation;
  nose.scale = core::Vec3(0.3f, 0.2f, 0.3f);
}

void MultiplayerSystem::drawOverlay(ecs::World &world) {
  auto &network = world.getResource<net::Network>();
  auto &session = world.getResource<net::Session>();

  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.45f);
  ImGui::Begin("Multiplayer", nullptr,
               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
  if (!network.isOpen()) {
    ImGui::Text("Network: %s", m_status.c_str());
  } else if (session.isActive()) {
    ImGui::Text("Session: peer %u%s", unsigned(session.self()),
                session.isLeader() ? " (leader)" : "");
    ImGui::Text("Listening on %s", network.localAddress().toString().c_str());
    ImGui::Text("Other players: %zu", m_avatars.size());
    if (!m_players->synced())
      ImGui::Text("Syncing...");
  } else {
    ImGui::Text("Session: %s", m_status.c_str());
  }
  ImGui::End();
}

void MultiplayerSystem::onDestroy(const ecs::SystemState &) {
  // Drop the shared set while the Session it points at is still alive.
  m_players.reset();
}

} // namespace multiplayer_demo
