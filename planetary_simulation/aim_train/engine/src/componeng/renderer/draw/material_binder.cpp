#include "componeng/renderer/draw/material_binder.hpp"

#include "componeng/renderer/asset/material.hpp"
#include "componeng/renderer/asset/pbr.hpp"
#include "componeng/renderer/asset/shader.hpp"
#include "componeng/renderer/asset/texture.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/utils/logger.hpp"

#include <type_traits>
#include <variant>

namespace componeng::renderer {

void MaterialBinder::setUniforms(const Shader &shader,
                                 const IMaterial &material) {
  for (const auto &[name, value] : material.getUniforms()) {
    const char *iname = name;
    std::visit(
        [&shader, iname](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, float>) {
            shader.setFloat(iname, arg);
          } else if constexpr (std::is_same_v<T, core::Vec2>) {
            shader.setVector2f(iname, arg);
          } else if constexpr (std::is_same_v<T, core::Vec3>) {
            shader.setVector3f(iname, arg);
          } else if constexpr (std::is_same_v<T, core::Vec4>) {
            shader.setVector4f(iname, arg);
          } else if constexpr (std::is_same_v<T, core::Mat4>) {
            shader.setMatrix4(iname, arg);
          }
        },
        value);
  }
}

void MaterialBinder::setTextureToggles(const Shader &shader, bool isPbr) {
  shader.setInteger("useTextures", isPbr ? 1 : 0);
  shader.setInteger("useNormalMap", isPbr ? 1 : 0);
}

void MaterialBinder::bindPbrTextures(api::IRenderDevice &device,
                                     const AssetManager &assets,
                                     const Shader &shader,
                                     const PbrMaterial &material,
                                     const Texture2D &albedo) {
  struct Slot {
    uint32_t unit;
    core::HandleID textureID;
    const char *sampler;
  };
  const Slot slots[] = {
      {1, material.getNormalMapID(), "normalMap"},
      {2, material.getMetallicMapID(), "metallicMap"},
      {3, material.getRoughnessMapID(), "roughnessMap"},
      {4, material.getAOMapID(), "aoMap"},
  };
  for (const Slot &slot : slots) {
    device.setActiveTextureUnit(slot.unit);
    if (slot.textureID != 0) {
      assets.getTexture(slot.textureID).bind();
    } else {
      albedo.bind();
    }
    shader.setInteger(slot.sampler, static_cast<int>(slot.unit));
  }
  // Back to unit 0 so the albedo binding the caller tracks stays the one that
  // later binds and draws see.
  device.setActiveTextureUnit(0);
}

void MaterialBinder::bindResources(api::IRenderDevice &device,
                                   const AssetManager &assets,
                                   const Shader &shader,
                                   const IMaterial &material, bool depthOnly) {
  auto report = [this](const std::string &key, const char *message,
                       const std::string &name) {
    if (m_reported.insert(key).second) {
      LOG_ERROR("%s: %s", message, name.c_str());
    }
  };

  uint32_t unit = api::kFirstMaterialTextureUnit;
  for (const auto &b : material.getTextureBindings()) {
    if (unit >= api::kTextureUnitLimit) {
      report("units:" + b.sampler, "out of texture units for sampler",
             b.sampler);
      break;
    }
    device.setActiveTextureUnit(unit);
    assets.getTexture(b.texture).bind();
    shader.setInteger(b.sampler.c_str(), static_cast<int>(unit));
    ++unit;
  }
  for (const auto &b : material.getBufferTextureBindings()) {
    if (unit >= api::kTextureUnitLimit) {
      report("units:" + b.sampler, "out of texture units for sampler",
             b.sampler);
      break;
    }
    device.bindBufferTexture(unit, assets.getBuffer(b.buffer), b.format);
    shader.setInteger(b.sampler.c_str(), static_cast<int>(unit));
    ++unit;
  }
  if (unit != api::kFirstMaterialTextureUnit) {
    // Back to unit 0 so the albedo binding the caller tracks stays the one
    // that later binds and draws see.
    device.setActiveTextureUnit(0);
  }

  uint32_t binding = api::kFirstMaterialUniformBinding;
  for (const auto &b : material.getUniformBlockBindings()) {
    if (binding >= api::kUniformBindingLimit) {
      report("bindings:" + b.block, "out of uniform buffer bindings for block",
             b.block);
      break;
    }
    if (shader.bindUniformBlock(b.block.c_str(), binding)) {
      device.bindUniformBuffer(binding, assets.getBuffer(b.buffer));
    } else if (!depthOnly) {
      report("block:" + b.block, "the shader declares no uniform block",
             b.block);
    }
    ++binding;
  }
}

} // namespace componeng::renderer
