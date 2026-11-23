#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/backend/vulkan/vk_core.hpp"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace componeng::renderer::vulkan {

class VulkanRenderDevice;

/**
 * A vertex + fragment program compiled GLSL -> SPIR-V at load time. loadGLSL()
 * rewrites the engine's GL-style source (loose uniforms, unlocated varyings)
 * into Vulkan GLSL and keeps a CPU shadow of each uniform block plus the
 * sampler -> texture-unit table; the device snapshots both at every draw.
 */
class VulkanShader final : public api::IShader {
public:
  VulkanShader(VulkanRenderDevice *device, std::shared_ptr<VulkanCore> core,
               uint64_t id);
  ~VulkanShader() override;

  void loadGLSL(const char *vertexSource, const char *fragmentSource,
                const char *geometrySource) override;
  void loadSPIRV(const char *vertexPath, const char *fragmentPath,
                 const char *geometryPath) override;
  void use() const override;
  void release() override;

  // Per-shader GPU timing; the device resolves every span at present().
  void beginGpuTimer() const override;
  void endGpuTimer() const override;
  double lastGpuTimeMs() const override;

  void setFloat(const char *name, float value) const override;
  void setInteger(const char *name, int value) const override;
  void setVector2f(const char *name, float x, float y) const override;
  void setVector3f(const char *name, float x, float y, float z) const override;
  void setVector4f(const char *name, float x, float y, float z,
                   float w) const override;
  void setMatrix4(const char *name, const float *matrix) const override;

  api::VertexLayout reflectInstanceLayout() const override;
  bool bindUniformBlock(const char *blockName, uint32_t binding,
                        size_t expectedSizeBytes) override;
  std::vector<core::Name> reflectActiveUniformNames() const override;

  // what the device needs to draw with this shader
  struct BlockInfo {
    std::string name;
    uint32_t binding = 0;                  // descriptor binding in set 0
    uint32_t engineBinding = UINT32_MAX;   // IRenderDevice::bindUniformBuffer slot
  };
  struct LooseBlock {
    uint32_t binding = UINT32_MAX; // UINT32_MAX: this stage has none
    uint32_t size = 0;
    mutable std::vector<unsigned char> data;
  };
  struct SamplerInfo {
    std::string name;
    uint32_t binding = 0;
    bool texelBuffer = false;
    mutable int unit = 0;
  };

  bool valid() const {
    return m_vertex != VK_NULL_HANDLE && m_fragment != VK_NULL_HANDLE;
  }
  uint64_t id() const {
    return m_id;
  }
  VkShaderModule vertexModule() const {
    return m_vertex;
  }
  VkShaderModule fragmentModule() const {
    return m_fragment;
  }
  VkDescriptorSetLayout setLayout() const {
    return m_setLayout;
  }
  VkPipelineLayout pipelineLayout() const {
    return m_pipelineLayout;
  }
  const std::vector<BlockInfo> &blocks() const {
    return m_blocks;
  }
  const LooseBlock &loose(int stage) const {
    return m_loose[stage];
  }
  const std::vector<SamplerInfo> &samplers() const {
    return m_samplers;
  }

private:
  void write(const char *name, const void *src, size_t bytes) const;

  VulkanRenderDevice *m_device;
  std::shared_ptr<VulkanCore> m_core;
  uint64_t m_id;

  VkShaderModule m_vertex = VK_NULL_HANDLE;
  VkShaderModule m_fragment = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

  std::vector<BlockInfo> m_blocks;
  std::array<LooseBlock, 2> m_loose; // [0] vertex stage, [1] fragment stage
  // Leaf uniform name ("time", "waves[2].speed") -> byte offset in each
  // stage's loose block, -1 where that stage does not declare it.
  std::unordered_map<std::string, std::array<int32_t, 2>> m_uniforms;
  std::vector<SamplerInfo> m_samplers;
  api::VertexLayout m_instanceLayout;
};

} // namespace componeng::renderer::vulkan
