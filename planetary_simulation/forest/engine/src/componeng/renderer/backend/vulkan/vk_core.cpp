#include "componeng/renderer/backend/vulkan/vk_core.hpp"

#include "componeng/utils/logger.hpp"

#include <algorithm>

namespace componeng::renderer::vulkan {

VulkanCore::~VulkanCore() {
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
    flushAllDeferred();
    destroyGpuAllocator(allocator);
    allocator = nullptr;
    if (timeline != VK_NULL_HANDLE)
      vkDestroySemaphore(device, timeline, nullptr);
    if (cmdPool != VK_NULL_HANDLE)
      vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyDevice(device, nullptr);
  }
  if (instance != VK_NULL_HANDLE) {
    if (messenger != VK_NULL_HANDLE) {
      auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
      if (fn)
        fn(instance, messenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
  }
}

bool VulkanCore::createMappedBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    Allocation &out, HostMemory memory) const {
  if (!createGpuBuffer(allocator, size, usage, memory, out)) {
    LOG_ERROR("Vulkan: out of memory allocating a %llu byte buffer",
              static_cast<unsigned long long>(size));
    return false;
  }
  return true;
}

void VulkanCore::destroyAllocation(Allocation &a) const {
  destroyGpuBuffer(allocator, a);
}

bool VulkanCore::createImage(const VkImageCreateInfo &info, bool dedicated,
                             ImageAlloc &out) const {
  return createGpuImage(allocator, info, dedicated, out);
}

void VulkanCore::destroyImage(ImageAlloc &image) const {
  destroyGpuImage(allocator, image);
}

void VulkanCore::waitSerial(uint64_t serial) {
  if (serial == 0 || serial >= recordingSerial || serial <= completedSerial)
    return;
  VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
  wi.semaphoreCount = 1;
  wi.pSemaphores = &timeline;
  wi.pValues = &serial;
  vkWaitSemaphores(device, &wi, UINT64_MAX);
  completedSerial = std::max(completedSerial, serial);
}

void VulkanCore::pollCompleted() {
  uint64_t value = 0;
  if (timeline != VK_NULL_HANDLE &&
      vkGetSemaphoreCounterValue(device, timeline, &value) == VK_SUCCESS)
    completedSerial = std::max(completedSerial, value);
}

void VulkanCore::defer(std::function<void()> fn) {
  m_deferred.push_back({recordingSerial, std::move(fn)});
}

void VulkanCore::flushDeferred() {
  std::vector<Deferred> keep;
  std::vector<Deferred> run;
  for (auto &d : m_deferred)
    (d.serial <= completedSerial ? run : keep).push_back(std::move(d));
  m_deferred.swap(keep);
  for (auto &d : run)
    d.fn();
}

void VulkanCore::flushAllDeferred() {
  std::vector<Deferred> work;
  work.swap(m_deferred);
  for (auto &d : work)
    d.fn();
}

VkCommandBuffer VulkanCore::beginOneShot() const {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = cmdPool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device, &ai, &cmd);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  return cmd;
}

void VulkanCore::endOneShot(VkCommandBuffer cmd) const {
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);
  vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
}

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout from,
                     VkImageLayout to, uint32_t baseMip, uint32_t levelCount,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                     VkImageAspectFlags aspect) {
  VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  b.srcStageMask = srcStage;
  b.srcAccessMask = srcAccess;
  b.dstStageMask = dstStage;
  b.dstAccessMask = dstAccess;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {aspect, baseMip, levelCount, 0, 1};
  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &b;
  vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace componeng::renderer::vulkan
