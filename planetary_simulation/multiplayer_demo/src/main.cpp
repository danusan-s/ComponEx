#include "componeng/camera/main_camera.hpp"
#include "componeng/core/default_plugins.hpp"
#include "componeng/core/engine.hpp"
#include "componeng/core/plugin.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/net/nat/nat_plugin.hpp"
#include "componeng/net/net_plugin.hpp"
#include "componeng/physics/collider_component.hpp"
#include "componeng/physics/rigidbody_component.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"

#include "local_player.hpp"
#include "multiplayer_system.hpp"
#include "net_types.hpp"
#include "player_controller.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace componeng;
using namespace multiplayer_demo;

constexpr float kGroundSize = 40.0f;

// The game, as a plugin: build() registers, ready() creates the starting scene.
class MultiplayerDemo : public core::IPlugin {
public:
  explicit MultiplayerDemo(LaunchConfig launch) : m_launch(std::move(launch)) {
  }

  void build(core::Engine &engine) override {
    ecs::World &world = engine.world();
    // The Network and Session resources and the system that pumps them come
    // from NetPlugin (added in main, before this plugin).
    world.setResource<LaunchConfig>(LaunchConfig(m_launch));

    world.registerComponent<LocalPlayerComponent>();
    world.registerSystem<PlayerController>(ecs::SystemGroup::Simulation);
    world.registerSystem<MultiplayerSystem>(ecs::SystemGroup::Simulation);
  }

  void ready(ecs::World &world) override {
    // A big cube as the ground, top surface at y = 0.
    ecs::EntityID ground = world.createEntity();
    world.addComponents(
        ground, renderer::MeshComponent{.meshName = "cube"},
        core::TransformComponent{
            .position = core::Vec3(0.0f, -0.5f, 0.0f),
            .scale = core::Vec3(kGroundSize, 1.0f, kGroundSize)},
        renderer::MaterialComponent{.materialName = "default_diffuse"},
        renderer::ColorComponent{.color = core::Vec4(0.45f, 0.5f, 0.45f, 1.0f)},
        physics::RigidBodyComponent{
            .type = physics::RigidBodyComponent::Type::Static,
            .mass = 0.0f,
            .restitution = 0.0f},
        physics::ColliderComponent{.type = physics::ColliderType::Box});
  }

private:
  LaunchConfig m_launch;
};

static void usage(const char *exe) {
  std::printf(
      "LAN / same machine:\n"
      "  %s                    host on UDP port %u, or join the instance\n"
      "                        already hosting it on this machine\n"
      "  %s <ip:port> [local]  join a host, e.g. 127.0.0.1:%u\n"
      "  %s --port <n>         host on port <n>\n"
      "Internet (STUN + invite codes; swap codes by hand, paste the other\n"
      "player's code into the terminal):\n"
      "  %s --nat              host\n"
      "  %s --nat --join [CODE]  join; CODE is the host's invite code (or paste\n"
      "                        it later)\n",
      exe, unsigned(kDefaultPort), exe, unsigned(kDefaultPort), exe, exe, exe);
}

int main(int argc, char **argv) {
  LaunchConfig launch;
  bool join = false;
  bool portGiven = false;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return 0;
    } else if (arg == "--nat") {
      launch.nat = true;
    } else if (arg == "--join") {
      join = true;
      // Optional value: the host's invite code.
      if (i + 1 < argc && argv[i + 1][0] != '-')
        launch.address = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      launch.port = uint16_t(std::atoi(argv[++i]));
      portGiven = true;
    } else {
      positional.push_back(arg);
    }
  }

  if (launch.nat) {
    launch.host = !join;
    if (join && !portGiven)
      launch.port = 0;
  } else if (!positional.empty()) {
    // <ip:port> [local port]: join directly.
    launch.host = false;
    launch.address = positional[0];
    launch.port = positional.size() >= 2 ? uint16_t(std::atoi(positional[1].c_str()))
                                         : 0;
  } else {
    launch.autoJoinLocal = !portGiven;
  }

  core::DefaultPlugins defaults;
  defaults.window.title = "ComponEx multiplayer demo";

  core::Engine engine;
  engine.add(defaults).add(net::NetPlugin{});
  if (launch.nat) {
    // Internet play: STUN and invite codes (creates the NatTraversal resource).
    engine.add(net::NatPlugin{});
  }
  engine.add(MultiplayerDemo(launch)).run();
  return 0;
}
