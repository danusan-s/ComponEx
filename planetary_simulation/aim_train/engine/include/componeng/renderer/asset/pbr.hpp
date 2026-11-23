#pragma once

#include "componeng/core/types.hpp"
#include "componeng/renderer/asset/diffuse.hpp"

namespace componeng::renderer {

/**
 * @brief Physically Based Rendering material.
 *
 * Supports Cook-Torrance PBR with:
 *   - albedo (base color) texture
 *   - normal map (tangent-space)
 *   - metallic map
 *   - roughness map
 *   - ambient occlusion map
 *
 * Per-instance data: instanceModel (mat4) + instanceColor (vec4), written by
 * DiffuseMaterial, whose per-instance layout this material shares.
 * Material uniforms: albedoMap, normalMap, metallicMap, roughnessMap, aoMap,
 *   lightDir, lightColor, cameraPos, ambientColor, ambientStrength,
 *   fogColor, fogDensity, fogStart, fogEnd, useTextures, useNormalMap
 */
class PbrMaterial : public DiffuseMaterial {
public:
  PbrMaterial(core::HandleID shaderID, core::HandleID textureID)
      : DiffuseMaterial(shaderID, textureID), m_normalMapID(0),
        m_metallicMapID(0), m_roughnessMapID(0), m_aoMapID(0) {
  }

  core::HandleID getNormalMapID() const override {
    return m_normalMapID;
  }
  core::HandleID getMetallicMapID() const override {
    return m_metallicMapID;
  }
  core::HandleID getRoughnessMapID() const override {
    return m_roughnessMapID;
  }
  core::HandleID getAOMapID() const override {
    return m_aoMapID;
  }

  void setNormalMapID(core::HandleID id) {
    m_normalMapID = id;
  }
  void setMetallicMapID(core::HandleID id) {
    m_metallicMapID = id;
  }
  void setRoughnessMapID(core::HandleID id) {
    m_roughnessMapID = id;
  }
  void setAOMapID(core::HandleID id) {
    m_aoMapID = id;
  }

private:
  core::HandleID m_normalMapID;
  core::HandleID m_metallicMapID;
  core::HandleID m_roughnessMapID;
  core::HandleID m_aoMapID;
};

} // namespace componeng::renderer
