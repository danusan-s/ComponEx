#pragma once

#include <vulkan/vulkan.h>

namespace componeng::renderer::vulkan {

/**
 * GPU memory sub-allocation over Vulkan Memory Allocator: buffers and images
 * share large blocks instead of one vkAllocateMemory each (drivers cap those
 * at ~4096). VMA is included in vma_impl.cpp only.
 */
class GpuAllocator; // opaque

/** A buffer and the memory it lives in. `mapped` is non-null for the host-
 *  visible buffers createGpuBuffer makes (persistently mapped, coherent). */
struct BufferAlloc {
  VkBuffer buffer = VK_NULL_HANDLE;
  void *allocation = nullptr; // opaque VmaAllocation
  void *mapped = nullptr;
  VkDeviceSize size = 0;
};

struct ImageAlloc {
  VkImage image = VK_NULL_HANDLE;
  void *allocation = nullptr; // opaque VmaAllocation
};

/** Which memory a host-visible buffer should prefer. */
enum class HostMemory {
  PreferDevice, // device-local host-visible (resizable BAR) when there is some:
                // fast for the GPU to read, and the CPU only ever writes it
  PreferHost,   // plain system memory: staging
};

GpuAllocator *createGpuAllocator(VkInstance instance, VkPhysicalDevice physical,
                                 VkDevice device);
void destroyGpuAllocator(GpuAllocator *allocator);

bool createGpuBuffer(GpuAllocator *allocator, VkDeviceSize size,
                     VkBufferUsageFlags usage, HostMemory memory,
                     BufferAlloc &out);
void destroyGpuBuffer(GpuAllocator *allocator, BufferAlloc &buffer);

/** Device-local image. `dedicated` gives it its own memory block (large
 *  render targets). */
bool createGpuImage(GpuAllocator *allocator, const VkImageCreateInfo &info,
                    bool dedicated, ImageAlloc &out);
void destroyGpuImage(GpuAllocator *allocator, ImageAlloc &image);

} // namespace componeng::renderer::vulkan
