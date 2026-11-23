#pragma once

#include "componeng/core/types.hpp"
#include "componeng/renderer/asset/material.hpp"
#include "componeng/renderer/asset/mesh.hpp"
#include "componeng/renderer/asset/pbr.hpp"
#include "componeng/renderer/asset/shader.hpp"
#include "componeng/renderer/asset/texture.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace componeng::renderer {

/**
 * @brief Asset manager for shaders, textures, and meshes.
 *
 * Loads assets from disk on first request, caches them by string handle,
 * and returns const references on subsequent lookups. All access is through
 * static methods — no public constructor exists.
 */
class AssetManager {
public:
  /** Assets create their GPU resources through `device`, which must outlive
   *  the manager. */
  explicit AssetManager(api::IRenderDevice &device) : m_device(&device) {
  }
  ~AssetManager() = default;

  // Non-copyable, Movable
  AssetManager(const AssetManager &) = delete;
  AssetManager &operator=(const AssetManager &) = delete;

  AssetManager(AssetManager &&) = default;
  AssetManager &operator=(AssetManager &&) = default;

  void loadShader(const char *vShaderFile, const char *fShaderFile,
                  const char *gShaderFile, core::Name name);
  const Shader &getShader(core::HandleID id) const;
  core::HandleID getShaderID(core::Name name) const;
  bool shaderExists(core::Name name) const;

  /** Calls `fn(name, shader)` for every loaded shader, in no fixed order. */
  template <typename Fn> void forEachShader(Fn &&fn) const {
    for (const auto &[name, id] : m_shaders)
      fn(name, *m_shaderResources[id - 1]);
  }

  /** Ends the frame's GPU timing for every shader and reads finished results
   *  (Shader::collectGpuTimer). Call once per frame. */
  void collectShaderTimings() const;

  /** Loads an image file as a texture. `alpha` keeps the file's alpha channel
   *  (RGBA); otherwise it is loaded as RGB. `desc` sets wrapping, filtering,
   *  mipmaps, anisotropy and colour space; the default matches the engine's
   *  original fixed behaviour. A file that cannot be read is logged and
   *  replaced by a 1x1 magenta texture so the failure is visible on screen. */
  void loadTexture(const char *file, bool alpha, core::Name name,
                   const api::TextureDesc &desc = {});
  const Texture2D &getTexture(core::HandleID id) const;
  core::HandleID getTextureID(core::Name name) const;
  bool textureExists(core::Name name) const;

  /** Creates a texture from pixels in memory (no file), registered under
   *  `name`. 8-bit formats take bytes, the 16F/32F formats take floats; see
   *  api::TextureFormat. `pixels` may be null to allocate storage and fill it
   *  later with updateTexture. For data textures (heights, masks) set
   *  `desc.mipmaps = false` and, if values must not be blended,
   *  Nearest filters. */
  void createTexture(core::Name name, uint32_t width, uint32_t height,
                     api::TextureFormat format, const void *pixels,
                     const api::TextureDesc &desc = {});

  /** Overwrites a width x height region at (x, y) of a texture made with
   *  createTexture, with pixels in the texture's format. Returns false (and
   *  logs) if the region is not inside the texture. */
  bool updateTexture(core::HandleID id, uint32_t x, uint32_t y, uint32_t width,
                     uint32_t height, const void *pixels);

  /** A GPU buffer registered under `name`, to be bound to a material as a
   *  uniform block (IMaterial::setUniformBuffer) or a texture buffer
   *  (IMaterial::setBufferTexture). `data` may be null to only size it. */
  void createBuffer(core::Name name, const void *data, size_t sizeBytes,
                    api::IBuffer::Usage usage = api::IBuffer::Usage::Dynamic);
  /** Overwrites part of a buffer made with createBuffer; the range must lie
   *  inside it. Returns false (and logs) otherwise. */
  bool updateBuffer(core::HandleID id, size_t offset, const void *data,
                    size_t sizeBytes);
  api::IBuffer &getBuffer(core::HandleID id) const;
  core::HandleID getBufferID(core::Name name) const;
  bool bufferExists(core::Name name) const;

  template <typename T>
  void registerMaterial(const char *name, const char *shaderName,
                        const char *textureName) {
    core::HandleID shaderID = getShaderID(shaderName);
    core::HandleID textureID = getTextureID(textureName);
    auto material = std::make_unique<T>(shaderID, textureID);

    const Shader &shader = getShader(shaderID);
    material->setVertexLayout(shader.reflectInstanceLayout());
    material->setKnownUniforms(shader.reflectActiveUniformNames());

    m_materialResources.push_back(std::move(material));
    core::HandleID id = m_materialResources.size();
    m_materials[name] = id;
  }

  /** Register a PBR material with albedo + optional
   * normal/metallic/roughness/AO maps. */
  template <typename T>
  void registerPbrMaterial(const char *name, const char *shaderName,
                           const char *albedoName,
                           const char *normalName = nullptr,
                           const char *metallicName = nullptr,
                           const char *roughnessName = nullptr,
                           const char *aoName = nullptr) {
    core::HandleID shaderID = getShaderID(shaderName);
    core::HandleID albedoID = getTextureID(albedoName);
    auto material = std::make_unique<T>(shaderID, albedoID);

    const Shader &shader = getShader(shaderID);
    material->setVertexLayout(shader.reflectInstanceLayout());
    material->setKnownUniforms(shader.reflectActiveUniformNames());

    if (normalName && textureExists(normalName))
      static_cast<T *>(material.get())
          ->setNormalMapID(getTextureID(normalName));
    if (metallicName && textureExists(metallicName))
      static_cast<T *>(material.get())
          ->setMetallicMapID(getTextureID(metallicName));
    if (roughnessName && textureExists(roughnessName))
      static_cast<T *>(material.get())
          ->setRoughnessMapID(getTextureID(roughnessName));
    if (aoName && textureExists(aoName))
      static_cast<T *>(material.get())->setAOMapID(getTextureID(aoName));

    m_materialResources.push_back(std::move(material));
    core::HandleID id = m_materialResources.size();
    m_materials[name] = id;
  }

  core::HandleID getMaterialID(core::Name name) const;
  IMaterial &getMaterial(core::HandleID id) const;

  void addMesh(core::Name name, std::unique_ptr<Mesh> mesh);
  void loadMesh(const char *file, core::Name name);
  const Mesh &getMesh(core::HandleID id) const;
  core::HandleID getMeshID(core::Name name) const;

  void clear();

private:
  api::IRenderDevice *m_device;

  std::unordered_map<core::Name, core::HandleID> m_shaders;
  std::unordered_map<core::Name, core::HandleID> m_textures;
  std::unordered_map<core::Name, core::HandleID> m_materials;
  std::unordered_map<core::Name, core::HandleID> m_meshes;
  std::unordered_map<core::Name, core::HandleID> m_buffers;

  std::vector<std::unique_ptr<Shader>> m_shaderResources;
  std::vector<std::unique_ptr<Texture2D>> m_textureResources;
  std::vector<std::unique_ptr<IMaterial>> m_materialResources;
  std::vector<std::unique_ptr<Mesh>> m_meshResources;
  std::vector<std::unique_ptr<api::IBuffer>> m_bufferResources;

  std::unique_ptr<Shader> loadShaderFromFile(const char *vShaderFile,
                                             const char *fShaderFile,
                                             const char *gShaderFile = nullptr);
  std::unique_ptr<Texture2D> loadTextureFromFile(const char *file, bool alpha,
                                                 const api::TextureDesc &desc);
  std::unique_ptr<Mesh> loadMeshFromFile(const char *file);
};

} // namespace componeng::renderer
