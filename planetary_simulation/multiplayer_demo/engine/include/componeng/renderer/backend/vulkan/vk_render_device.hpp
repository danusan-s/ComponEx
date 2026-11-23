#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/backend/vulkan/vk_core.hpp"
#include "componeng/renderer/backend/vulkan/vk_gpu_timer.hpp"
#include "componeng/renderer/backend/vulkan/vk_render_target.hpp"
#include "componeng/renderer/backend/vulkan/vk_resources.hpp"
#include "componeng/renderer/backend/vulkan/vk_shader.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace componeng::renderer::vulkan {

/**
 * Vulkan 1.3 implementation of IRenderDevice, using dynamic rendering (no
 * render passes). Up to kFramesInFlight frames are recorded ahead of the GPU
 * on one thread; each frame slot owns its command buffer, descriptor pools
 * and uniform ring, and buffers the CPU rewrites track the last frame that
 * read them (VulkanBuffer::markUsed). The GL-style state the engine sets is
 * snapshotted into a descriptor set at every draw; pipelines are created on
 * demand and cached. The viewport is flipped with a negative height to match
 * GL's coordinates. See dev_docs/vulkan for the design.
 */
class VulkanRenderDevice final : public api::IRenderDevice {
public:
  VulkanRenderDevice() = default;
  ~VulkanRenderDevice() override;

  VulkanRenderDevice(const VulkanRenderDevice &) = delete;
  VulkanRenderDevice &operator=(const VulkanRenderDevice &) = delete;

  void init(void *windowHandle) override;
  void shutdown() override;
  void setViewport(int x, int y, int w, int h) override;
  void setScissor(int x, int y, int w, int h) override;
  api::Extent viewportExtent() const override;
  void clear(float r, float g, float b, float a) override;
  void present(void *windowHandle) override;
  int checkErrors() const override {
    return 0;
  }

  // GPU timing (see VulkanGpuTimer).
  void beginGpuTimer() override {
    m_timer.beginFrameSpan();
  }
  void endGpuTimer() override {
    m_timer.endFrameSpan();
  }
  double lastGpuTimeMs() const override {
    return m_timer.frameMs();
  }
  void beginGpuPass(api::PassId pass) override {
    if (m_timer.supported() && ensureFrame())
      m_timer.beginPass(pass);
  }
  void endGpuPass(api::PassId pass) override {
    m_timer.endPass(pass);
  }
  double lastGpuPassTimeMs(api::PassId pass) const override {
    return m_timer.passMs(pass);
  }
  // Per-shader spans, forwarded from VulkanShader.
  void beginShaderTimer(uint64_t shaderId) {
    if (m_timer.supported() && ensureFrame())
      m_timer.beginShader(shaderId);
  }
  void endShaderTimer(uint64_t shaderId) {
    m_timer.endShader(shaderId);
  }
  double shaderTimeMs(uint64_t shaderId) const {
    return m_timer.shaderMs(shaderId);
  }

  void initImGui(void *windowHandle) override;
  void imguiNewFrame() override;
  void imguiRender() override;
  void shutdownImGui() override;

  std::unique_ptr<api::IShader> createShader() override;
  std::unique_ptr<api::ITexture> createTexture() override;
  std::unique_ptr<api::IMesh> createMesh() override;
  std::unique_ptr<api::IBuffer> createBuffer() override;
  std::unique_ptr<api::IRenderTarget>
  createRenderTarget(const api::RenderTargetDesc &desc) override;
  void setRenderTarget(api::IRenderTarget *target) override;

  /** The frame slot being recorded (see VulkanRenderTarget). */
  uint32_t frameSlot() const {
    return m_slot;
  }

  void setRenderState(const api::RenderState &state) override {
    m_state = state;
  }
  void bindUniformBuffer(uint32_t binding, api::IBuffer &buffer) override;
  void setActiveTextureUnit(uint32_t unit) override {
    m_activeUnit = unit < api::kTextureUnitLimit ? unit : 0;
  }
  void bindBufferTexture(uint32_t unit, api::IBuffer &buffer,
                         api::BufferTextureFormat format) override;
  void setupInstanceAttributes(api::IBuffer &instanceBuffer,
                               const api::VertexLayout &layout) override;
  void unbindInstanceAttributes(const api::VertexLayout &layout) override;
  void drawFullscreenTriangle() override;
  void drawIndexedInstanced(size_t indexCount, uint32_t instanceCount) override;

  /**
   * For a resource about to record texture-upload commands that read
   * `stagingBytes` of staging memory: returns the frame's command buffer
   * (outside any render pass) so the upload rides along with the frame. Null
   * if no frame can be opened or too much staging is queued; the caller then
   * does a blocking submit.
   */
  VkCommandBuffer beginTransfer(size_t stagingBytes);

  // Called by the resources' bind()/use().
  void bindTexture(const VulkanTexture *texture) {
    m_unitTexture[m_activeUnit] = texture;
  }
  void bindMesh(const VulkanMesh *mesh) {
    m_mesh = mesh;
  }
  void bindShader(const VulkanShader *shader) {
    m_shader = shader;
  }

private:
  // setup
  bool createInstance();
  bool pickPhysicalDevice();
  bool createDevice();
  bool createSwapchain();
  void destroySwapchain(bool keepHandle);
  bool createDepthImage();
  void createFrameObjects();
  /** Pick up frames the GPU has finished: free what they held, read timings. */
  void pollCompletedFrames();

  // frame
  VkExtent2D currentExtent() const;
  bool ensureFrame();
  bool ensureRendering();
  void beginPass(bool colorOnly);
  void endRendering();
  void recordViewportAndScissor();

  // drawing
  bool draw(bool indexed, uint32_t count, uint32_t instanceCount);
  VkPipeline pipelineFor(const VulkanShader &shader, const VulkanMesh *mesh,
                         bool instanced);
  VkDescriptorSet buildDescriptors(const VulkanShader &shader);
  VkDescriptorSet allocateSet(VkDescriptorSetLayout layout);
  bool allocUniform(uint32_t size, VkBuffer &buffer, VkDeviceSize &offset,
                    void *&mapped);

  std::shared_ptr<VulkanCore> m_core;
  GLFWwindow *m_window = nullptr;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;

  // swapchain + depth
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  VkFormat m_swapFormat = VK_FORMAT_UNDEFINED;
  VkExtent2D m_extent{};
  std::vector<VkImage> m_images;
  std::vector<VkImageView> m_imageViews;
  std::vector<VkSemaphore> m_renderDone;
  VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
  // One depth image per frame slot: with a single shared one, frame N+1's depth
  // tests would have to wait for frame N's, serialising the two on the GPU.
  struct DepthTarget {
    ImageAlloc image;
    VkImageView view = VK_NULL_HANDLE;
  };
  DepthTarget m_depth[kFramesInFlight];
  bool m_swapDirty = false;

  // Everything that is per frame in flight.
  struct FrameSlot {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    uint64_t serial = 0; // the frame last submitted from this slot (0: none)
    // descriptor pools and the loose-uniform upload ring
    std::vector<VkDescriptorPool> pools;
    size_t poolIndex = 0;
    std::vector<VulkanCore::Allocation> uniformChunks;
    size_t uniformChunk = 0;
    VkDeviceSize uniformOffset = 0;
  };
  FrameSlot m_slots[kFramesInFlight];
  uint32_t m_slot = 0; // slot of the frame being recorded / recorded next
  FrameSlot &slot() {
    return m_slots[m_slot];
  }

  // frame being recorded
  VkCommandBuffer m_cmd = VK_NULL_HANDLE;
  uint32_t m_imageIndex = 0;
  bool m_frameActive = false;
  bool m_rendering = false;
  bool m_colorInitialised = false;
  bool m_depthInitialised = false;
  size_t m_stagingBytes = 0; // staging queued into the current frame
  // The render target being drawn into (null: the window), and what the window
  // had drawn while a target is bound.
  VulkanRenderTarget *m_target = nullptr;
  bool m_windowColorInitialised = false;
  bool m_windowDepthInitialised = false;
  bool m_clearPending = false;
  float m_clearColor[4] = {0, 0, 0, 1};

  // GL-style state the engine sets between draws
  api::RenderState m_state;
  int m_viewport[4] = {0, 0, 0, 0};
  int m_scissor[4] = {0, 0, 0, 0};
  bool m_viewportSet = false;
  bool m_scissorSet = false;
  uint32_t m_activeUnit = 0;
  const VulkanTexture *m_unitTexture[api::kTextureUnitLimit] = {};
  struct UnitBuffer {
    VulkanBuffer *buffer = nullptr;
    api::BufferTextureFormat format = api::BufferTextureFormat::R32F;
  };
  UnitBuffer m_unitBuffer[api::kTextureUnitLimit];
  VulkanBuffer *m_uniformBuffers[api::kUniformBindingLimit] = {};
  const VulkanShader *m_shader = nullptr;
  const VulkanMesh *m_mesh = nullptr;
  VulkanBuffer *m_instanceBuffer = nullptr;
  api::VertexLayout m_instanceLayout;

  std::unordered_map<std::string, VkPipeline> m_pipelines;
  uint64_t m_nextShaderId = 1;

  std::unique_ptr<VulkanTexture> m_whiteTexture;
  std::unique_ptr<VulkanBuffer> m_dummyBuffer;
  VulkanGpuTimer m_timer;
  bool m_imguiReady = false;
  bool m_shutDown = false;
};

} // namespace componeng::renderer::vulkan
