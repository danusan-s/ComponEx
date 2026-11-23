#pragma once

#include "componeng/ecs/component_serializer.hpp"

namespace componeng::renderer {

/**
 * @brief Draws an entity's mesh `count` times from one entity, with no
 * per-instance data: the vertex shader works out each copy's placement itself
 * from gl_InstanceID, the per-frame values (camera, time) and material
 * uniforms.
 *
 * Use it for things too numerous for one entity each (grass, debris, rain),
 * where creating, culling and batching a million entities every frame would
 * cost far more than drawing them. Next to the usual TransformComponent,
 * MeshComponent and MaterialComponent, add this and the entity becomes one
 * instanced draw call of `count` instances.
 *
 *  - The material's shader must not declare per-instance attributes
 *    (`instanceModel`, ...): there is no instance buffer, so they would read
 *    zero. The engine logs an error if it finds some.
 *  - Each such entity is its own draw call; unlike ordinary entities it is not
 *    merged with others sharing a mesh and material, since the instance count
 *    and gl_InstanceID range belong to the entity.
 *  - The entity's transform is used only for culling (see below), not passed to
 *    the shader.
 *  - `cull` is off by default, because placement done in the shader (a field of
 *    grass following the camera) can land anywhere, so the mesh's bounds say
 *    nothing about where it draws. Turn it on for a static patch whose
 *    instances stay inside the mesh bounds under the entity's transform, plus
 *    the material's bounds padding.
 */
struct ProceduralInstancesComponent {
  static constexpr const char *component_name = "ProceduralInstancesComponent";

  int count = 0;
  bool cull = false;
};

} // namespace componeng::renderer

namespace componeng::ecs {

#define PROCEDURAL_INSTANCES_COMPONENT_FIELDS(F, ctx)                          \
  F(count, ctx)                                                                \
  F(cull, ctx)

SERIALIZABLE_COMPONENT(renderer::ProceduralInstancesComponent,
                       PROCEDURAL_INSTANCES_COMPONENT_FIELDS)

#undef PROCEDURAL_INSTANCES_COMPONENT_FIELDS

} // namespace componeng::ecs
