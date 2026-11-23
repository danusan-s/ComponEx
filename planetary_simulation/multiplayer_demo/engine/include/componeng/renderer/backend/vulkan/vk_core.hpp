#pragma once

#include "componeng/renderer/backend/vulkan/vk_memory.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace componeng::renderer::vulkan {

/** How many frames the CPU may record ahead of the GPU. */
constexpr uint32_t kFramesInFlight = 2;

/**
 * The Vulkan objects every resource needs, shared (shared_ptr) by the device
 * and all its resources so the VkDevice outlives them in any teardown order.
 */
struct VulkanCore {
  VulkanCore() = default;
  ~VulkanCore();
  VulkanCore(const VulkanCore &) = delete;
  VulkanCore &operator=(const VulkanCore &) = delete;

  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool cmdPool = VK_NULL_HANDLE;

  // frame progress
  // Each submitted frame gets a serial (1, 2, 3, ...) that the GPU signals on
  // `timeline` when it finishes. `recordingSerial` is the serial the frame
  // being recorded will get.
  VkSemaphore timeline = VK_NULL_HANDLE;
  uint64_t recordingSerial = 1;
  uint64_t completedSerial = 0;

  /** Block until frame `serial` has finished. Returns at once for 0, the frame
   *  being recorded, and finished frames. */
  void waitSerial(uint64_t serial);
  /** Refresh completedSerial from the GPU without blocking. */
  void pollCompleted();

  VkPhysicalDeviceProperties props{};
  VkPhysicalDeviceMemoryProperties memProps{};
  bool anisotropy = false;

  /** Sub-allocator for every buffer and image; created and destroyed with the device. */
  GpuAllocator *allocator = nullptr;

  /** A persistently mapped, coherent, host-visible buffer. */
  using Allocation = BufferAlloc;
  bool createMappedBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          Allocation &out,
                          HostMemory memory = HostMemory::PreferDevice) const;
  void destroyAllocation(Allocation &a) const;

  bool createImage(const VkImageCreateInfo &info, bool dedicated,
                   ImageAlloc &out) const;
  void destroyImage(ImageAlloc &image) const;

  /** Runs `fn` once every frame that may still use what it destroys is done. */
  void defer(std::function<void()> fn);
  /** Runs the deferred work whose frames have completed. */
  void flushDeferred();
  /** Runs all of it; only valid once the device is idle. */
  void flushAllDeferred();

  /** Record and submit commands now, blocking until they finish. */
  VkCommandBuffer beginOneShot() const;
  void endOneShot(VkCommandBuffer cmd) const;

private:
  struct Deferred {
    uint64_t serial;
    std::function<void()> fn;
  };
  std::vector<Deferred> m_deferred;
};

/** sync2 barrier over `levelCount` mips of a colour image. */
void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout from,
                     VkImageLayout to, uint32_t baseMip, uint32_t levelCount,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                     VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

} // namespace componeng::renderer::vulkan
