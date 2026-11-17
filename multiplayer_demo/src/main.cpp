#include "componeng/camera/main_camera.hpp"
#include "componeng/core/engine.hpp"
#include "componeng/core/game.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/net/network_system.hpp"
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

using namespace componeng;
using namespace multiplayer_demo;

constexpr float kGroundSize = 40.0f;

class MultiplayerDemo : public core::IGame {
public:
  explicit MultiplayerDemo(LaunchConfig launch) : m_launch(std::move(launch)) {
  }

  void init(ecs::World &world) override {
    // Opt in to networking: creates the Network and Session resources and the
    // system that updates them every frame.
    net::installNetworking(world);
    world.setResource<LaunchConfig>(LaunchConfig(m_launch));

    world.registerComponent<LocalPlayerComponent>();
    world.registerSystem<PlayerController>(ecs::SystemGroup::Simulation);
    world.registerSystem<MultiplayerSystem>(ecs::SystemGroup::Simulation);

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

  void shutdown(ecs::World &) override {
  }

private:
  LaunchConfig m_launch;
};

static void usage(const char *exe) {
  std::printf("Usage:\n"
              "  %s                      host on UDP port %u, or join the instance\n"
              "                          already hosting it on this machine\n"
              "  %s <ip:port>            join a session, e.g. 127.0.0.1:%u\n"
              "  %s <ip:port> <local>    join, using local UDP port <local>\n"
              "  %s --port <n>           host on port <n>\n",
              exe, unsigned(kDefaultPort), exe, unsigned(kDefaultPort), exe,
              exe);
}

int main(int argc, char **argv) {
  LaunchConfig launch;
  launch.autoJoinLocal = argc < 2;
  if (argc >= 2) {
    std::string first = argv[1];
    if (first == "-h" || first == "--help") {
      usage(argv[0]);
      return 0;
    }
    if (first == "--port" && argc >= 3) {
      launch.port = uint16_t(std::atoi(argv[2]));
    } else {
      launch.host = false;
      launch.address = first;
      launch.port = argc >= 3 ? uint16_t(std::atoi(argv[2])) : 0;
    }
  }

  core::Engine &engine = core::Engine::get();
  engine.init();

  MultiplayerDemo game(launch);
  engine.run(game);

  engine.shutdown();
  return 0;
}
