#include "componeng/renderer/lod/lod_system.hpp"

#include "componeng/camera/camera_component.hpp"
#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/renderer/lod/lod_component.hpp"
#include "componeng/resources/render_settings.hpp"

#include <algorithm>
#include <cmath>

namespace componeng::renderer {

namespace {

core::HandleID meshAt(const LodComponent &lod, int level) {
  return level >= 0 && level < LodComponent::kMaxLevels ? lod.mesh[level] : 0;
}

// Writes the entity's MeshComponent for the LOD state: the settled level, or
// the mid-fade pair (see MeshComponent::fade).
void applyLevel(MeshComponent &mesh, const LodComponent &lod) {
  const core::HandleID to = meshAt(lod, lod.level);
  const core::HandleID from = meshAt(lod, lod.fromLevel);

  if (lod.transition < 1.0f && to != from && (to != 0 || from != 0)) {
    mesh.visible = true;
    if (to != 0) {
      // Dithering in; the mesh being replaced (if any) dithers out beside it.
      mesh.meshID = to;
      mesh.fadeMeshID = from;
      mesh.fade = lod.transition - 1.0f;
    } else {
      // Fading to nothing: the old mesh alone dithers out.
      mesh.meshID = from;
      mesh.fadeMeshID = 0;
      mesh.fade = lod.transition;
    }
    return;
  }

  mesh.fade = 0.0f;
  mesh.fadeMeshID = 0;
  if (to == 0) {
    mesh.visible = false;
  } else {
    mesh.visible = true;
    mesh.meshID = to;
  }
}

} // namespace

void LodSystem::onUpdate(const ecs::SystemState &state) {
  ecs::World &world = *state.world;
  const ecs::EntityID cameraEntity =
      world.getResource<camera::MainCamera>().entity;
  const core::Vec3 cam =
      world.readComponent<core::TransformComponent>(cameraEntity).position;
  // Screen-height mode: height on screen = size / (2 * distance * tanHalfFov),
  // so a level holds while size >= threshold * 2 * tanHalfFov * distance.
  const float twoTanHalfFov =
      2.0f *
      std::tan(
          0.5f *
          core::radians(
              world.readComponent<camera::CameraComponent>(cameraEntity).fov));

  // Cross-fade: the share of a full transition one frame covers (1 = instant).
  float fadeStep = 1.0f;
  bool dither = true;
  if (world.hasResource<resources::RenderSettings>()) {
    const auto &settings = world.getResource<resources::RenderSettings>();
    dither = settings.lodDither && settings.lodFadeSeconds > 0.0f;
    if (dither) {
      fadeStep = state.deltaTime / settings.lodFadeSeconds;
    }
  }

  world.query()
      .read<core::TransformComponent>()
      .write<MeshComponent, LodComponent>()
      .eachParallel(
          world.threadPool(), [cam, twoTanHalfFov, dither, fadeStep](
                                  const core::TransformComponent &transform,
                                  MeshComponent &mesh, LodComponent &lod) {
            const float dx = transform.position.x - cam.x;
            const float dy = transform.position.y - cam.y;
            const float dz = transform.position.z - cam.z;
            const float distSq = dx * dx + dy * dy + dz * dz;

            // First level that has a mesh and whose range still
            // covers us; kMaxLevels means "beyond all of them".
            int level = LodComponent::kMaxLevels;
            if (lod.size > 0.0f) {
              // Screen-height mode: the entity's apparent height as
              // a fraction of the screen's against each threshold.
              const core::Vec3 &s = transform.scale;
              const float worldSize =
                  lod.size *
                  std::max({std::fabs(s.x), std::fabs(s.y), std::fabs(s.z)});
              const float coverage =
                  worldSize / (twoTanHalfFov * std::sqrt(distSq) + 1e-6f);
              for (int k = 0; k < LodComponent::kMaxLevels; ++k) {
                if (lod.mesh[k] != 0 && coverage >= lod.screenHeight[k]) {
                  level = k;
                  break;
                }
              }
            } else {
              for (int k = 0; k < LodComponent::kMaxLevels; ++k) {
                if (lod.mesh[k] != 0 &&
                    distSq < lod.distance[k] * lod.distance[k]) {
                  level = k;
                  break;
                }
              }
            }

            const bool changed = level != lod.level;
            if (!changed && lod.transition >= 1.0f) {
              return;
            }

            if (changed) {
              if (!dither || lod.level < 0) {
                // Nothing to fade from (first frame) or fading is off.
                lod.transition = 1.0f;
              } else if (lod.transition < 1.0f && level == lod.fromLevel) {
                // Back to the level being faded out: run the fade backwards.
                lod.fromLevel = lod.level;
                lod.transition = 1.0f - lod.transition;
              } else {
                lod.fromLevel = lod.level;
                lod.transition = 0.0f;
              }
              lod.level = level;
            }
            if (!dither) {
              lod.transition = 1.0f;
            } else if (lod.transition < 1.0f) {
              lod.transition = std::min(1.0f, lod.transition + fadeStep);
            }
            applyLevel(mesh, lod);
          });
}

} // namespace componeng::renderer
