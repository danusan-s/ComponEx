#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/backend/vulkan/vk_core.hpp"
#include "componeng/renderer/backend/vulkan/vk_resources.hpp"

#include <array>
#include <memory>

namespace componeng::renderer::vulkan {

class VulkanRenderDevice;

/**
 * A colour image (the swapchain's format, so existing pipelines fit) and a
 * depth image per frame slot: with one shared set, a frame could overwrite
 * what the frame before it is still sampling. The device records the barriers;
 * the target only owns the images and the per-slot state it needs.
 */
class VulkanRenderTarget final : public api::IRenderTarget {
public:
  struct Slot {
    ImageAlloc color;
    VkImageView colorView = VK_NULL_HANDLE;
    ImageAlloc depth;
    VkImageView depthView = VK_NULL_HANDLE;
    // The frame (VulkanCore::recordingSerial) that last bound this slot, and
    // where it stands in that frame.
    uint64_t frameSerial = 0;
    VkImageLayout colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool colorInitialised = false;
    bool depthInitialised = false;
  };

  VulkanRenderTarget(VulkanRenderDevice *device,
                     std::shared_ptr<VulkanCore> core,
                     const api::RenderTargetDesc &desc, VkFormat colorFormat,
                     VkFormat depthFormat);
  ~VulkanRenderTarget() override;

  /** Points the returned texture at the frame being recorded's image. */
  api::ITexture &colorTexture() override;

  bool valid() const {
    return m_valid;
  }
  Slot &slot(uint32_t index) {
    return m_slots[index];
  }

private:
  bool createSlot(Slot &slot, VkFormat colorFormat, VkFormat depthFormat);

  VulkanRenderDevice *m_device;
  std::shared_ptr<VulkanCore> m_core;
  std::array<Slot, kFramesInFlight> m_slots;
  VkSampler m_sampler = VK_NULL_HANDLE;
  VulkanTexture m_texture;
  bool m_valid = false;
};

} // namespace componeng::renderer::vulkan
