#pragma once

#include "componeng/core/types.hpp"
#include "componeng/ecs/component_serializer.hpp"

namespace componeng::renderer {

struct MeshComponent {
  static constexpr const char *component_name = "MeshComponent";

  core::Name meshName;

  // Runtime data (not serialized)
  core::HandleID meshID = 0;
  // Set by the application to hide an entity; never touched by the engine.
  bool visible = true;
  // Set every frame by CullingSystem when the entity is outside the camera
  // frustum. Kept apart from `visible` so culling cannot un-hide (or hide)
  // anything the application decided.
  bool culled = false;

  // Set by LodSystem during a LOD cross-fade (RenderSettings::lodDither), else
  // 0 / 0. `fade` is `meshID`'s instanceFade: negative while it dithers in
  // (-1 invisible .. 0 shown), positive while it dithers out (0 shown .. 1
  // gone). `fadeMeshID` is the other mesh, drawn with the complementary value
  // (0: none). Not applied to ProceduralInstancesComponent entities.
  float fade = 0.0f;
  core::HandleID fadeMeshID = 0;
};

} // namespace componeng::renderer

namespace componeng::ecs {

#define MESH_COMPONENT_FIELDS(F, ctx) F(meshName, ctx)

SERIALIZABLE_COMPONENT(renderer::MeshComponent, MESH_COMPONENT_FIELDS)

#undef MESH_COMPONENT_FIELDS

} // namespace componeng::ecs
