#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/backend/vulkan/vk_core.hpp"

#include <memory>
#include <vector>

namespace componeng::renderer::vulkan {

class VulkanRenderDevice;

/**
 * Host-visible buffer written by plain memcpy. The GPU may still be reading
 * the previous frame, so a buffer remembers the last frame that bound it
 * (markUsed). A write arriving while that frame is in flight either
 *  - renames (Dynamic buffers): swaps to another backing allocation, refilled
 *    from a CPU shadow copy, and retires the old one until its frame ends; or
 *  - waits (Static buffers: no shadow, written rarely).
 * So buffers rewritten every frame never stall the CPU.
 */
class VulkanBuffer final : public api::IBuffer {
public:
  explicit VulkanBuffer(std::shared_ptr<VulkanCore> core);
  ~VulkanBuffer() override;

  void setData(const void *data, size_t sizeBytes, Usage usage) override;
  void setSubData(size_t offset, const void *data, size_t sizeBytes) override;
  size_t size() const override {
    return m_size;
  }
  void release() override;

  /** The backing the frame being recorded should bind. Changes when a write
   *  renames the buffer, so fetch it at draw time. */
  VkBuffer handle() const {
    return m_current.alloc.buffer;
  }
  /** Allocated bytes (>= size()). */
  VkDeviceSize capacity() const {
    return m_current.alloc.size;
  }
  /** Record that the frame being recorded (`serial`) reads this buffer. */
  void markUsed(uint64_t serial) const {
    m_current.lastUse = serial;
  }
  /** A texel-buffer view of the whole current backing in `format`. */
  VkBufferView view(api::BufferTextureFormat format);

private:
  struct Backing {
    VulkanCore::Allocation alloc;
    VkBufferView views[3] = {};
    uint64_t lastUse = 0; // last frame serial that bound it
  };

  void destroyBacking(Backing &backing);
  /** Make the backing writable. Returns true if it was swapped (refill it from
   *  the shadow), false if it can be written in place, possibly after a wait. */
  bool prepareWrite();

  std::shared_ptr<VulkanCore> m_core;
  mutable Backing m_current;
  std::vector<Backing> m_retired;
  std::vector<unsigned char> m_shadow; // contents, when the buffer is Dynamic
  bool m_shadowed = false;
  size_t m_size = 0;
};

class VulkanTexture final : public api::ITexture {
public:
  VulkanTexture(VulkanRenderDevice *device, std::shared_ptr<VulkanCore> core);
  ~VulkanTexture() override;

  void generate(uint32_t width, uint32_t height, const unsigned char *data,
                bool alpha, const api::TextureDesc &desc) override;
  void create(uint32_t width, uint32_t height, api::TextureFormat format,
              const void *pixels, const api::TextureDesc &desc) override;
  void update(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
              const void *pixels) override;
  void bind() const override;
  void release() override;

  VkImageView view() const {
    return m_view;
  }
  VkSampler sampler() const {
    return m_sampler;
  }

  /** Makes this texture a view of an image someone else owns (a render
   *  target's): the view and sampler are borrowed and never destroyed here. */
  void alias(VkImageView view, VkSampler sampler, uint32_t width,
             uint32_t height) {
    m_view = view;
    m_sampler = sampler;
    m_width = width;
    m_height = height;
  }

private:
  /** Copies `pixels` (m_format layout, w x h at x, y) into the image and
   *  rebuilds the mip chain; `fromUndefined` for the very first fill. Recorded
   *  into the frame's command buffer (no CPU wait), or run as a blocking
   *  one-shot if no frame can be opened. */
  void upload(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
              const void *pixels, bool fromUndefined);

  VulkanRenderDevice *m_device;
  std::shared_ptr<VulkanCore> m_core;
  VkImage m_image = VK_NULL_HANDLE;
  void *m_allocation = nullptr; // opaque, owned through VulkanCore::createImage
  VkImageView m_view = VK_NULL_HANDLE;
  VkSampler m_sampler = VK_NULL_HANDLE;
  VkFormat m_format = VK_FORMAT_UNDEFINED;
  uint32_t m_mipLevels = 1;
  // RGB8 is stored as RGBA8 (Vulkan has no portable 3-channel sampled format).
  bool m_expandRgb = false;
  size_t m_cpuPixelBytes = 0;
};

class VulkanMesh final : public api::IMesh {
public:
  VulkanMesh(VulkanRenderDevice *device, std::shared_ptr<VulkanCore> core);
  ~VulkanMesh() override;

  void upload(const float *vertices, size_t vertexCount,
              const uint32_t *indices, size_t indexCount,
              const api::VertexLayout &layout) override;
  void bind() const override;
  void release() override;
  size_t indexCount() const override {
    return m_indexCount;
  }

  const VulkanBuffer *vertexBuffer() const {
    return m_vertices.get();
  }
  const VulkanBuffer *indexBuffer() const {
    return m_indices.get();
  }
  const api::VertexLayout &layout() const {
    return m_layout;
  }

private:
  VulkanRenderDevice *m_device;
  std::shared_ptr<VulkanCore> m_core;
  std::unique_ptr<VulkanBuffer> m_vertices;
  std::unique_ptr<VulkanBuffer> m_indices;
  api::VertexLayout m_layout;
  size_t m_indexCount = 0;
};

} // namespace componeng::renderer::vulkan
