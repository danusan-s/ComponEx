// The only translation unit that includes VMA; built without warnings (vendored).

#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include "third_party/vk_mem_alloc.h"

#include "componeng/renderer/backend/vulkan/vk_memory.hpp"

namespace componeng::renderer::vulkan {

namespace {
VmaAllocator vma(GpuAllocator *a) {
  return reinterpret_cast<VmaAllocator>(a);
}
} // namespace

GpuAllocator *createGpuAllocator(VkInstance instance, VkPhysicalDevice physical,
                                 VkDevice device) {
  VmaAllocatorCreateInfo ci = {};
  ci.vulkanApiVersion = VK_API_VERSION_1_3;
  ci.instance = instance;
  ci.physicalDevice = physical;
  ci.device = device;
  VmaAllocator allocator = VK_NULL_HANDLE;
  if (vmaCreateAllocator(&ci, &allocator) != VK_SUCCESS)
    return nullptr;
  return reinterpret_cast<GpuAllocator *>(allocator);
}

void destroyGpuAllocator(GpuAllocator *allocator) {
  if (allocator)
    vmaDestroyAllocator(vma(allocator));
}

bool createGpuBuffer(GpuAllocator *allocator, VkDeviceSize size,
                     VkBufferUsageFlags usage, HostMemory memory,
                     BufferAlloc &out) {
  if (size == 0)
    size = 16;
  VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo ai = {};
  ai.usage = memory == HostMemory::PreferDevice ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                                                : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
  ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
             VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  // The backend writes with plain memcpy and never flushes, so it needs
  // coherent memory.
  ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  VmaAllocation allocation = VK_NULL_HANDLE;
  VmaAllocationInfo info = {};
  if (vmaCreateBuffer(vma(allocator), &bi, &ai, &out.buffer, &allocation, &info) !=
      VK_SUCCESS)
    return false;
  out.allocation = allocation;
  out.mapped = info.pMappedData;
  out.size = size;
  return true;
}

void destroyGpuBuffer(GpuAllocator *allocator, BufferAlloc &buffer) {
  if (buffer.buffer != VK_NULL_HANDLE)
    vmaDestroyBuffer(vma(allocator), buffer.buffer,
                     static_cast<VmaAllocation>(buffer.allocation));
  buffer = BufferAlloc{};
}

bool createGpuImage(GpuAllocator *allocator, const VkImageCreateInfo &info,
                    bool dedicated, ImageAlloc &out) {
  VmaAllocationCreateInfo ai = {};
  ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  ai.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  if (dedicated)
    ai.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  VmaAllocation allocation = VK_NULL_HANDLE;
  if (vmaCreateImage(vma(allocator), &info, &ai, &out.image, &allocation,
                     nullptr) != VK_SUCCESS)
    return false;
  out.allocation = allocation;
  return true;
}

void destroyGpuImage(GpuAllocator *allocator, ImageAlloc &image) {
  if (image.image != VK_NULL_HANDLE)
    vmaDestroyImage(vma(allocator), image.image,
                    static_cast<VmaAllocation>(image.allocation));
  image = ImageAlloc{};
}

} // namespace componeng::renderer::vulkan
