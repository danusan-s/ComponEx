#include "aim_system.hpp"

#include "helper.hpp"
#include "orb_component.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <limits>
#include <vector>

#include <imgui/imgui.h>

#include "componeng/components/collider_component.hpp"
#include "componeng/components/transform_component.hpp"
#include "componeng/core/debug_ui.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/resources/audio_engine.hpp"
#include "componeng/resources/input_state.hpp"
#include "componeng/resources/main_camera.hpp"

namespace aim_train {

using namespace componeng;

static void drawCrosshair() {
  ImGuiIO &io = ImGui::GetIO();
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  float size = 12.0f;
  float gap = 4.0f;
  float thickness = 2.0f;
  ImU32 color = IM_COL32(255, 255, 255, 200);

  dl->AddLine(ImVec2(c.x - size, c.y), ImVec2(c.x - gap, c.y), color,
              thickness);
  dl->AddLine(ImVec2(c.x + gap, c.y), ImVec2(c.x + size, c.y), color,
              thickness);
  dl->AddLine(ImVec2(c.x, c.y - size), ImVec2(c.x, c.y - gap), color,
              thickness);
  dl->AddLine(ImVec2(c.x, c.y + gap), ImVec2(c.x, c.y + size), color,
              thickness);
}

static core::Vec3 cameraFront(core::Vec3 rotation) {
  float pitch = core::radians(rotation.x);
  float yaw = core::radians(rotation.y);
  core::Vec3 front;
  front.x = cos(yaw) * cos(pitch);
  front.y = sin(pitch);
  front.z = sin(yaw) * cos(pitch);
  return core::normalize(front);
}

bool raySphereIntersect(core::Vec3 origin, core::Vec3 dir, core::Vec3 center,
                        float radius, float &t) {
  core::Vec3 oc = origin - center;
  float a = core::dot(dir, dir);
  float b = 2.0f * core::dot(oc, dir);
  float c = core::dot(oc, oc) - radius * radius;
  float discriminant = b * b - 4.0f * a * c;
  if (discriminant < 0.0f)
    return false;
  float sqrtD = std::sqrt(discriminant);
  float t1 = (-b - sqrtD) / (2.0f * a);
  float t2 = (-b + sqrtD) / (2.0f * a);
  t = (t1 > 0.0f) ? t1 : t2;
  return t > 0.0f;
}

void AimSystem::onUpdate(const ecs::SystemState &state) {
  auto &input = state.world->get_resource<resources::InputState>();
  auto &mainCam = state.world->get_resource<resources::MainCamera>();
  auto &camTransform =
      state.world->getComponent<components::TransformComponent>(mainCam.entity);

  drawCrosshair();
  core::DebugUI::addValue("Score", static_cast<float>(m_score));

  float dt = state.deltaTime;
  state.world->query<components::TransformComponent, OrbComponent>().each(
      [dt](components::TransformComponent &t, OrbComponent &o) {
        o.phase += o.speed * dt;
        t.position.x = cos(o.phase) * o.radius;
        t.position.z = sin(o.phase) * o.radius;
        t.position.y = o.centerY;
      });

  if (!input.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT))
    return;

  core::Vec3 rayOrigin = camTransform.position;
  core::Vec3 rayDir = cameraFront(camTransform.rotation);

  struct Target {
    ecs::EntityID id;
    core::Vec3 center;
    float radius;
  };
  std::vector<Target> targets;

  auto q = state.world->query<components::TransformComponent,
                              components::ColliderComponent, OrbComponent>();
  q.eachWithEntity([&](ecs::EntityID entity, components::TransformComponent &t,
                       components::ColliderComponent &c, OrbComponent &) {
    if (c.type == components::ColliderType::Sphere) {
      targets.push_back({entity, t.position + c.transform.position,
                         t.scale.x * c.transform.scale.x});
    }
  });

  float closestT = std::numeric_limits<float>::max();
  ecs::EntityID hitEntity = ecs::INVALID_ENTITY;

  for (auto &target : targets) {
    float t;
    if (raySphereIntersect(rayOrigin, rayDir, target.center, target.radius,
                           t) &&
        t < closestT) {
      closestT = t;
      hitEntity = target.id;
    }
  }
  auto &assetManager = state.world->get_resource<renderer::AssetManager>();
  auto &audioEngine = state.world->get_resource<resources::AudioEngine>();
  auto hitSoundPath = assetManager.getAudio(assetManager.getAudioID("boop"));
  auto shotSoundPath =
      assetManager.getAudio(assetManager.getAudioID("gunshot"));

  audioEngine.playSoundFromFile(shotSoundPath, rayOrigin.x, rayOrigin.y,
                                rayOrigin.z, 0.3f, 1.0f, false, 1.0f, 100.0f);

  if (hitEntity != ecs::INVALID_ENTITY) {
    core::Vec3 hitPos = rayOrigin + rayDir * closestT;
    audioEngine.playSoundFromFile(hitSoundPath, hitPos.x, hitPos.y, hitPos.z,
                                  0.8f, 1.0f, false, 1.0f, 100.0f);
    state.world->destroyEntity(hitEntity);
    spawnOrb(*state.world);
    ++m_score;
  }
}

} // namespace aim_train
