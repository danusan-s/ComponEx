#pragma once

#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "material.hpp"

namespace componeng::renderer {

class DiffuseMaterial : public IMaterial {
public:
  // Vertex layout is not declared here — AssetManager::registerMaterial
  // reflects it straight off the compiled shader's active attributes.
  using IMaterial::IMaterial;

  void resolveInstanceSlots(const api::VertexLayout &layout) override {
    m_modelSlot = findInstanceMatrixSlot(layout, m_modelName);
    m_colorSlot = findInstanceSlot(layout, m_colorName);
  }

  void writeInstanceData(const InstanceContext &ctx,
                         InstanceWriter &out) const override {
    out.set(m_modelSlot, ctx.modelMatrix());

    const auto *color =
        ctx.world.tryGetComponent<renderer::ColorComponent>(ctx.entity);
    out.set(m_colorSlot, color ? color->color : core::Vec4(1.0f));
  }

private:
  const core::Name m_modelName{"instanceModel"};
  const core::Name m_colorName{"instanceColor"};
  InstanceMatrixSlot m_modelSlot;
  InstanceSlot m_colorSlot;
};

} // namespace componeng::renderer
