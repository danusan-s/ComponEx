#include "componeng/renderer/asset_manager.hpp"

#include "componeng/core/types.hpp"
#include "componeng/utils/logger.hpp"

#include <fstream>
#include <sstream>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace componeng::renderer {

void AssetManager::loadShader(const char *vShaderFile, const char *fShaderFile,
                              const char *gShaderFile, core::Name name) {
  LOG_INFO("Loading Shader: %s", name.c_str());
  m_shaderResources.push_back(
      loadShaderFromFile(vShaderFile, fShaderFile, gShaderFile));
  core::HandleID id = m_shaderResources.size();
  m_shaders[name] = id;
}

void AssetManager::collectShaderTimings() const {
  for (const auto &shader : m_shaderResources)
    shader->collectGpuTimer();
}

const Shader &AssetManager::getShader(core::HandleID id) const {
  return *m_shaderResources.at(id - 1);
}

bool AssetManager::shaderExists(core::Name name) const {
  return m_shaders.find(name) != m_shaders.end();
}

core::HandleID AssetManager::getShaderID(core::Name name) const {
  if (m_shaders.find(name) == m_shaders.end()) {
    LOG_ERROR("Shader not found: %s", name.c_str());
    throw std::runtime_error("Shader not found");
  }
  return m_shaders.at(name);
}

void AssetManager::loadTexture(const char *file, bool alpha, core::Name name,
                               const api::TextureDesc &desc) {
  LOG_INFO("Loading Texture: %s", name.c_str());
  m_textureResources.push_back(loadTextureFromFile(file, alpha, desc));
  core::HandleID id = m_textureResources.size();
  m_textures[name] = id;
}

const Texture2D &AssetManager::getTexture(core::HandleID id) const {
  return *m_textureResources.at(id - 1);
}

core::HandleID AssetManager::getTextureID(core::Name name) const {
  if (m_textures.find(name) == m_textures.end()) {
    LOG_ERROR("Texture not found: %s", name.c_str());
    throw std::runtime_error("Texture not found");
  }
  return m_textures.at(name);
}

bool AssetManager::textureExists(core::Name name) const {
  return m_textures.find(name) != m_textures.end();
}

void AssetManager::createTexture(core::Name name, uint32_t width,
                                 uint32_t height, api::TextureFormat format,
                                 const void *pixels,
                                 const api::TextureDesc &desc) {
  LOG_INFO("Creating Texture: %s (%ux%u)", name.c_str(), width, height);
  auto texture = std::make_unique<Texture2D>();
  texture->create(*m_device, width, height, format, pixels, desc);
  m_textureResources.push_back(std::move(texture));
  core::HandleID id = m_textureResources.size();
  m_textures[name] = id;
}

bool AssetManager::updateTexture(core::HandleID id, uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height,
                                 const void *pixels) {
  return m_textureResources.at(id - 1)->update(x, y, width, height, pixels);
}

void AssetManager::createBuffer(core::Name name, const void *data,
                                size_t sizeBytes, api::IBuffer::Usage usage) {
  LOG_INFO("Creating Buffer: %s (%zu bytes)", name.c_str(), sizeBytes);
  auto buffer = m_device->createBuffer();
  buffer->setData(data, sizeBytes, usage);
  m_bufferResources.push_back(std::move(buffer));
  core::HandleID id = m_bufferResources.size();
  m_buffers[name] = id;
}

bool AssetManager::updateBuffer(core::HandleID id, size_t offset,
                                const void *data, size_t sizeBytes) {
  api::IBuffer &buffer = *m_bufferResources.at(id - 1);
  if (offset > buffer.size() || sizeBytes > buffer.size() - offset) {
    LOG_ERROR("Buffer update range (%zu + %zu) is outside the %zu-byte buffer",
              offset, sizeBytes, buffer.size());
    return false;
  }
  buffer.setSubData(offset, data, sizeBytes);
  return true;
}

api::IBuffer &AssetManager::getBuffer(core::HandleID id) const {
  return *m_bufferResources.at(id - 1);
}

core::HandleID AssetManager::getBufferID(core::Name name) const {
  if (m_buffers.find(name) == m_buffers.end()) {
    LOG_ERROR("Buffer not found: %s", name.c_str());
    throw std::runtime_error("Buffer not found");
  }
  return m_buffers.at(name);
}

bool AssetManager::bufferExists(core::Name name) const {
  return m_buffers.find(name) != m_buffers.end();
}

core::HandleID AssetManager::getMaterialID(core::Name name) const {
  if (m_materials.find(name) == m_materials.end()) {
    LOG_ERROR("Material not found: %s", name.c_str());
    throw std::runtime_error("Material not found");
  }
  return m_materials.at(name);
}

IMaterial &AssetManager::getMaterial(core::HandleID id) const {
  return *m_materialResources.at(id - 1);
}

void AssetManager::addMesh(core::Name name, std::unique_ptr<Mesh> mesh) {
  LOG_INFO("Adding Mesh: %s", name.c_str());
  mesh->uploadToGPU(*m_device);
  m_meshResources.push_back(std::move(mesh));
  core::HandleID id = m_meshResources.size();
  m_meshes[name] = id;
}

void AssetManager::loadMesh(const char *file, core::Name name) {
  LOG_INFO("Loading Model: %s", name.c_str());
  m_meshResources.push_back(loadMeshFromFile(file));
  core::HandleID id = m_meshResources.size();
  m_meshes[name] = id;
}

const Mesh &AssetManager::getMesh(core::HandleID id) const {
  return *m_meshResources.at(id - 1);
}

core::HandleID AssetManager::getMeshID(core::Name name) const {
  if (m_meshes.find(name) == m_meshes.end()) {
    LOG_ERROR("Mesh not found: %s", name.c_str());
    throw std::runtime_error("Mesh not found");
  }
  return m_meshes.at(name);
}

std::unique_ptr<Shader> AssetManager::loadShaderFromFile(
    const char *vShaderFile, const char *fShaderFile, const char *gShaderFile) {
  std::string vertexCode;
  std::string fragmentCode;
  std::string geometryCode;
  try {
    std::ifstream vertexShaderFile(vShaderFile);
    std::ifstream fragmentShaderFile(fShaderFile);
    std::stringstream vShaderStream, fShaderStream;
    vShaderStream << vertexShaderFile.rdbuf();
    fShaderStream << fragmentShaderFile.rdbuf();
    vertexShaderFile.close();
    fragmentShaderFile.close();
    vertexCode = vShaderStream.str();
    fragmentCode = fShaderStream.str();
    if (gShaderFile != nullptr) {
      std::ifstream geometryShaderFile(gShaderFile);
      std::stringstream gShaderStream;
      gShaderStream << geometryShaderFile.rdbuf();
      geometryShaderFile.close();
      geometryCode = gShaderStream.str();
    }
  } catch (const std::exception &e) {
    LOG_ERROR("ERROR::SHADER: Failed to read shader files");
  }
  auto shader = std::make_unique<Shader>(*m_device);
  shader->compile(vertexCode.c_str(), fragmentCode.c_str(),
                  gShaderFile != nullptr ? geometryCode.c_str() : nullptr);
  return shader;
}

std::unique_ptr<Texture2D>
AssetManager::loadTextureFromFile(const char *file, bool alpha,
                                  const api::TextureDesc &desc) {
  LOG_INFO("Loading texture file: %s", file);
  int width = 0, height = 0, nrChannels = 0;
  unsigned char *data =
      stbi_load(file, &width, &height, &nrChannels, alpha ? 4 : 3);

  auto texture = std::make_unique<Texture2D>();
  if (!data) {
    LOG_ERROR("Failed to load texture '%s': %s", file, stbi_failure_reason());
    // A bright magenta texel makes a missing file obvious in the scene
    // instead of sampling uninitialised memory.
    unsigned char magenta[4] = {255, 0, 255, 255};
    texture->generate(*m_device, 1, 1, magenta, alpha, desc);
    return texture;
  }

  texture->generate(*m_device, width, height, data, alpha, desc);
  stbi_image_free(data);
  return texture;
}

std::unique_ptr<Mesh> AssetManager::loadMeshFromFile(const char *file) {
  std::string modelData;
  try {
    std::ifstream modelWavefrontObjFile(file);
    std::stringstream wavefrontObjStream;
    wavefrontObjStream << modelWavefrontObjFile.rdbuf();
    modelWavefrontObjFile.close();
    modelData = wavefrontObjStream.str();
  } catch (const std::exception &e) {
    LOG_ERROR("ERROR::MODEL: Failed to read model file");
  }

  auto mesh = std::make_unique<Mesh>();
  mesh->generateFromWavefrontObj(modelData);
  mesh->uploadToGPU(*m_device);
  return mesh;
}

void AssetManager::clear() {
  LOG_INFO("Deleting loaded resources");

  m_shaders.clear();
  m_shaderResources.clear();
  m_textures.clear();
  m_textureResources.clear();
  m_meshes.clear();
  m_meshResources.clear();
  m_buffers.clear();
  m_bufferResources.clear();
}

} // namespace componeng::renderer
