#pragma once

#include <string>
#include <unordered_set>

namespace componeng::renderer {

class AssetManager;
class IMaterial;
class PbrMaterial;
class Shader;
class Texture2D;
namespace api {
class IRenderDevice;
}

/**
 * @brief Gets a material's own data to the GPU for a draw: its loose uniforms,
 * its extra textures / texture buffers / uniform blocks, and the PBR texture
 * set. Which pass is drawing, and what state is already bound, is the
 * caller's business.
 */
class MaterialBinder {
public:
  /** The material's uniforms (IMaterial::getUniforms) as loose uniforms. */
  static void setUniforms(const Shader &shader, const IMaterial &material);

  /** Texture toggles the shaded shaders read: PBR materials use their
   *  textures, a plain diffuse material does not. */
  static void setTextureToggles(const Shader &shader, bool isPbr);

  /** PBR textures on units 1-4 (normal, metallic, roughness, AO), each
   *  falling back to `albedo` when the material has none. Leaves unit 0
   *  active. */
  static void bindPbrTextures(api::IRenderDevice &device,
                              const AssetManager &assets, const Shader &shader,
                              const PbrMaterial &material,
                              const Texture2D &albedo);

  /** Binds the extra textures, texture buffers and uniform blocks a material
   *  declared (IMaterial::setTexture and friends). Problems are reported
   *  once. `depthOnly` is for a depth pre-pass program, which holds only the
   *  vertex stage: a block or sampler only the fragment shader reads is
   *  expected to be missing there and is skipped without a report. */
  void bindResources(api::IRenderDevice &device, const AssetManager &assets,
                     const Shader &shader, const IMaterial &material,
                     bool depthOnly);

private:
  // Problems already logged, so each is reported once rather than every frame.
  std::unordered_set<std::string> m_reported;
};

} // namespace componeng::renderer
