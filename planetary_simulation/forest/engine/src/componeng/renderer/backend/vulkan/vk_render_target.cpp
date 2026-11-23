#include "componeng/renderer/backend/vulkan/vk_render_target.hpp"

#include "componeng/renderer/backend/vulkan/vk_render_device.hpp"
#include "componeng/utils/logger.hpp"

namespace componeng::renderer::vulkan {

VulkanRenderTarget::VulkanRenderTarget(VulkanRenderDevice *device,
                                       std::shared_ptr<VulkanCore> core,
                                       const api::RenderTargetDesc &desc,
                                       VkFormat colorFormat,
                                       VkFormat depthFormat)
    : m_device(device), m_core(core), m_texture(device, core) {
  m_width = desc.width;
  m_height = desc.height;

  m_valid = true;
  for (Slot &slot : m_slots) {
    if (!createSlot(slot, colorFormat, depthFormat)) {
      m_valid = false;
      LOG_ERROR("Vulkan: could not create a %ux%u render target", m_width,
                m_height);
      return;
    }
  }

  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = VK_FILTER_LINEAR;
  si.minFilter = VK_FILTER_LINEAR;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  si.addressModeU = si.addressModeV = si.addressModeW =
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
  vkCreateSampler(m_core->device, &si, nullptr, &m_sampler);
}

bool VulkanRenderTarget::createSlot(Slot &slot, VkFormat colorFormat,
                                    VkFormat depthFormat) {
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.extent = {m_width, m_height, 1};
  ii.mipLevels = 1;
  ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  ii.format = colorFormat;
  ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  if (!m_core->createImage(ii, /*dedicated=*/true, slot.color))
    return false;
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = slot.color.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = colorFormat;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (vkCreateImageView(m_core->device, &vi, nullptr, &slot.colorView) !=
      VK_SUCCESS)
    return false;

  ii.format = depthFormat;
  ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  if (!m_core->createImage(ii, /*dedicated=*/true, slot.depth))
    return false;
  vi.image = slot.depth.image;
  vi.format = depthFormat;
  vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
  return vkCreateImageView(m_core->device, &vi, nullptr, &slot.depthView) ==
         VK_SUCCESS;
}

VulkanRenderTarget::~VulkanRenderTarget() {
  // A frame still being drawn by the GPU may be using these.
  VulkanCore *core = m_core.get();
  const std::array<Slot, kFramesInFlight> slots = m_slots;
  const VkSampler sampler = m_sampler;
  core->defer([core, slots, sampler]() mutable {
    vkDestroySampler(core->device, sampler, nullptr);
    for (Slot s : slots) {
      vkDestroyImageView(core->device, s.colorView, nullptr);
      vkDestroyImageView(core->device, s.depthView, nullptr);
      if (s.color.image != VK_NULL_HANDLE)
        core->destroyImage(s.color);
      if (s.depth.image != VK_NULL_HANDLE)
        core->destroyImage(s.depth);
    }
  });
}

api::ITexture &VulkanRenderTarget::colorTexture() {
  m_texture.alias(m_slots[m_device->frameSlot()].colorView, m_sampler, m_width,
                  m_height);
  return m_texture;
}

} // namespace componeng::renderer::vulkan
