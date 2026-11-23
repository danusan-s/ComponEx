#pragma once

#include "componeng/core/types.hpp"
#include "componeng/ecs/component_serializer.hpp"

namespace componeng::renderer {

struct MaterialComponent {
  static constexpr const char *component_name = "MaterialComponent";

  core::Name materialName;

  // Runtime data (not serialized)
  core::HandleID materialID = 0;
};

struct ColorComponent {
  static constexpr const char *component_name = "ColorComponent";

  core::Vec4 color = core::Vec4(1.0f);
};

} // namespace componeng::renderer

namespace componeng::ecs {

#define MATERIAL_COMPONENT_FIELDS(F, ctx) F(materialName, ctx)

SERIALIZABLE_COMPONENT(renderer::MaterialComponent, MATERIAL_COMPONENT_FIELDS)

#undef MATERIAL_COMPONENT_FIELDS

#define COLOR_COMPONENT_FIELDS(F, ctx) F(color, ctx)

SERIALIZABLE_COMPONENT(renderer::ColorComponent, COLOR_COMPONENT_FIELDS)

#undef COLOR_COMPONENT_FIELDS

} // namespace componeng::ecs
