#include "componeng/renderer/backend/vulkan/vk_render_device.hpp"

#include "componeng/utils/logger.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace componeng::renderer::vulkan {

namespace {

constexpr VkDeviceSize kUniformChunkBytes = 1 << 20;
constexpr VkDeviceSize kDummyBufferBytes = 16384;

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
              VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT *data,
              void *) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    LOG_ERROR("[vulkan] %s", data->pMessage);
  else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    LOG_INFO("[vulkan] %s", data->pMessage);
  return VK_FALSE;
}

VkFormat attributeFormat(uint32_t components) {
  switch (components) {
    case 1:
      return VK_FORMAT_R32_SFLOAT;
    case 2:
      return VK_FORMAT_R32G32_SFLOAT;
    case 3:
      return VK_FORMAT_R32G32B32_SFLOAT;
    default:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
  }
}

template <typename T> void keyAdd(std::string &key, T v) {
  key.append(reinterpret_cast<const char *>(&v), sizeof(v));
}

} // namespace

VulkanRenderDevice::~VulkanRenderDevice() {
  shutdown();
}

// Setup

bool VulkanRenderDevice::createInstance() {
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "ComponEng";
  app.pEngineName = "ComponEng";
  app.apiVersion = VK_API_VERSION_1_3;

  uint32_t count = 0;
  const char **glfwExt = glfwGetRequiredInstanceExtensions(&count);
  std::vector<const char *> extensions(glfwExt, glfwExt + count);
  std::vector<const char *> layers;

  const bool wantValidation = std::getenv("COMPONENG_VK_VALIDATION") != nullptr;
  if (wantValidation) {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> props(n);
    vkEnumerateInstanceLayerProperties(&n, props.data());
    for (const auto &p : props) {
      if (std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        break;
      }
    }
  }

  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  ci.ppEnabledExtensionNames = extensions.data();
  ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
  ci.ppEnabledLayerNames = layers.data();
  if (vkCreateInstance(&ci, nullptr, &m_core->instance) != VK_SUCCESS) {
    LOG_ERROR("Vulkan: vkCreateInstance failed");
    return false;
  }

  if (!layers.empty()) {
    VkDebugUtilsMessengerCreateInfoEXT mi{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    mi.pfnUserCallback = debugCallback;
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_core->instance, "vkCreateDebugUtilsMessengerEXT"));
    if (fn)
      fn(m_core->instance, &mi, nullptr, &m_core->messenger);
    LOG_INFO("Vulkan validation layer enabled");
  }
  return true;
}

bool VulkanRenderDevice::pickPhysicalDevice() {
  uint32_t n = 0;
  vkEnumeratePhysicalDevices(m_core->instance, &n, nullptr);
  std::vector<VkPhysicalDevice> devices(n);
  vkEnumeratePhysicalDevices(m_core->instance, &n, devices.data());

  int bestScore = -1;
  for (VkPhysicalDevice pd : devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    if (props.apiVersion < VK_API_VERSION_1_3)
      continue;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, exts.data());
    const bool hasSwapchain =
        std::any_of(exts.begin(), exts.end(), [](const auto &e) {
          return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
        });
    if (!hasSwapchain)
      continue;

    VkPhysicalDeviceVulkan13Features f13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f13.pNext = &f12;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(pd, &f2);
    if (!f13.dynamicRendering || !f13.synchronization2 ||
        !f13.shaderDemoteToHelperInvocation || !f12.timelineSemaphore)
      continue;

    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, families.data());
    int family = -1;
    for (uint32_t i = 0; i < qCount; ++i) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, m_surface, &present);
      if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
        family = static_cast<int>(i);
        break;
      }
    }
    if (family < 0)
      continue;

    const int score =
        props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 0;
    if (score > bestScore) {
      bestScore = score;
      m_core->physical = pd;
      m_core->queueFamily = static_cast<uint32_t>(family);
    }
  }
  if (m_core->physical == VK_NULL_HANDLE) {
    LOG_ERROR("Vulkan: no GPU with Vulkan 1.3, dynamic rendering and presentation");
    return false;
  }
  vkGetPhysicalDeviceProperties(m_core->physical, &m_core->props);
  vkGetPhysicalDeviceMemoryProperties(m_core->physical, &m_core->memProps);
  LOG_INFO("Vulkan device: %s", m_core->props.deviceName);
  return true;
}

bool VulkanRenderDevice::createDevice() {
  VkPhysicalDeviceFeatures supported;
  vkGetPhysicalDeviceFeatures(m_core->physical, &supported);
  m_core->anisotropy = supported.samplerAnisotropy == VK_TRUE;

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qi.queueFamilyIndex = m_core->queueFamily;
  qi.queueCount = 1;
  qi.pQueuePriorities = &priority;

  VkPhysicalDeviceVulkan13Features f13{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  f13.dynamicRendering = VK_TRUE;
  f13.synchronization2 = VK_TRUE;
  f13.shaderDemoteToHelperInvocation = VK_TRUE; // glslang compiles `discard` to it
  VkPhysicalDeviceVulkan12Features f12{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  f12.timelineSemaphore = VK_TRUE;
  f13.pNext = &f12;
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  f2.pNext = &f13;
  f2.features.samplerAnisotropy = supported.samplerAnisotropy;

  const char *ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  di.pNext = &f2;
  di.queueCreateInfoCount = 1;
  di.pQueueCreateInfos = &qi;
  di.enabledExtensionCount = 1;
  di.ppEnabledExtensionNames = &ext;
  if (vkCreateDevice(m_core->physical, &di, nullptr, &m_core->device) !=
      VK_SUCCESS) {
    LOG_ERROR("Vulkan: vkCreateDevice failed");
    return false;
  }
  vkGetDeviceQueue(m_core->device, m_core->queueFamily, 0, &m_core->queue);

  m_core->allocator =
      createGpuAllocator(m_core->instance, m_core->physical, m_core->device);
  if (!m_core->allocator) {
    LOG_ERROR("Vulkan: could not create the memory allocator");
    return false;
  }

  // One timeline semaphore carries frame progress: each submit signals its
  // serial, and anything that needs a frame finished waits for that value.
  VkSemaphoreTypeCreateInfo ti{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  ti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  ti.initialValue = 0;
  VkSemaphoreCreateInfo tsi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  tsi.pNext = &ti;
  if (vkCreateSemaphore(m_core->device, &tsi, nullptr, &m_core->timeline) !=
      VK_SUCCESS)
    return false;

  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pi.queueFamilyIndex = m_core->queueFamily;
  return vkCreateCommandPool(m_core->device, &pi, nullptr, &m_core->cmdPool) ==
         VK_SUCCESS;
}

void VulkanRenderDevice::createFrameObjects() {
  for (FrameSlot &fs : m_slots) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = m_core->cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_core->device, &ai, &fs.cmd);

    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(m_core->device, &si, nullptr, &fs.imageAvailable);
  }
}

bool VulkanRenderDevice::createDepthImage() {
  for (DepthTarget &depth : m_depth) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = m_depthFormat;
    ii.extent = {m_extent.width, m_extent.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!m_core->createImage(ii, /*dedicated=*/true, depth.image))
      return false;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = depth.image.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = m_depthFormat;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(m_core->device, &vi, nullptr, &depth.view) != VK_SUCCESS)
      return false;
  }
  return true;
}

bool VulkanRenderDevice::createSwapchain() {
  VkDevice dev = m_core->device;
  VkPhysicalDevice pd = m_core->physical;

  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, m_surface, &caps);

  uint32_t n = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(pd, m_surface, &n, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(n);
  vkGetPhysicalDeviceSurfaceFormatsKHR(pd, m_surface, &n, formats.data());
  // UNORM, not sRGB: the GL path writes straight to a linear default framebuffer
  // and the shaders encode their own output.
  VkSurfaceFormatKHR chosen = formats.at(0);
  for (const auto &f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
      chosen = f;
      break;
    }
  }

  vkGetPhysicalDeviceSurfacePresentModesKHR(pd, m_surface, &n, nullptr);
  std::vector<VkPresentModeKHR> modes(n);
  vkGetPhysicalDeviceSurfacePresentModesKHR(pd, m_surface, &n, modes.data());
  auto has = [&](VkPresentModeKHR m) {
    return std::find(modes.begin(), modes.end(), m) != modes.end();
  };
  // The GL path runs with swap interval 0.
  VkPresentModeKHR mode = has(VK_PRESENT_MODE_IMMEDIATE_KHR)   ? VK_PRESENT_MODE_IMMEDIATE_KHR
                          : has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
                                                              : VK_PRESENT_MODE_FIFO_KHR;

  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(m_window, &fbw, &fbh);
  VkExtent2D extent = caps.currentExtent;
  if (extent.width == UINT32_MAX) {
    extent.width = std::clamp(static_cast<uint32_t>(fbw), caps.minImageExtent.width,
                              caps.maxImageExtent.width);
    extent.height = std::clamp(static_cast<uint32_t>(fbh), caps.minImageExtent.height,
                               caps.maxImageExtent.height);
  }
  if (extent.width == 0 || extent.height == 0)
    return false;

  uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount > 0)
    imageCount = std::min(imageCount, caps.maxImageCount);

  VkSwapchainKHR old = m_swapchain;
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.surface = m_surface;
  ci.minImageCount = imageCount;
  ci.imageFormat = chosen.format;
  ci.imageColorSpace = chosen.colorSpace;
  ci.imageExtent = extent;
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.preTransform = caps.currentTransform;
  ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                          ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                          : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  ci.presentMode = mode;
  ci.clipped = VK_TRUE;
  ci.oldSwapchain = old;
  VkSwapchainKHR created = VK_NULL_HANDLE;
  if (vkCreateSwapchainKHR(dev, &ci, nullptr, &created) != VK_SUCCESS) {
    LOG_ERROR("Vulkan: vkCreateSwapchainKHR failed");
    return false;
  }

  // The old chain (and what hangs off it) goes once the new one exists.
  vkDeviceWaitIdle(dev);
  destroySwapchain(/*keepHandle=*/false);
  m_swapchain = created;
  m_swapFormat = chosen.format;
  m_extent = extent;

  vkGetSwapchainImagesKHR(dev, m_swapchain, &n, nullptr);
  m_images.resize(n);
  vkGetSwapchainImagesKHR(dev, m_swapchain, &n, m_images.data());
  m_imageViews.resize(n);
  m_renderDone.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = m_images[i];
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = m_swapFormat;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(dev, &vi, nullptr, &m_imageViews[i]);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(dev, &si, nullptr, &m_renderDone[i]);
  }
  if (!createDepthImage())
    return false;
  if (m_imguiReady)
    ImGui_ImplVulkan_SetMinImageCount(std::max(2u, caps.minImageCount));
  m_swapDirty = false;
  return true;
}

void VulkanRenderDevice::destroySwapchain(bool keepHandle) {
  VkDevice dev = m_core->device;
  for (VkSemaphore s : m_renderDone)
    vkDestroySemaphore(dev, s, nullptr);
  for (VkImageView v : m_imageViews)
    vkDestroyImageView(dev, v, nullptr);
  m_renderDone.clear();
  m_imageViews.clear();
  m_images.clear();
  for (DepthTarget &depth : m_depth) {
    if (depth.view)
      vkDestroyImageView(dev, depth.view, nullptr);
    depth.view = VK_NULL_HANDLE;
    m_core->destroyImage(depth.image);
  }
  if (!keepHandle && m_swapchain) {
    vkDestroySwapchainKHR(dev, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
  }
}

void VulkanRenderDevice::init(void *windowHandle) {
  m_window = static_cast<GLFWwindow *>(windowHandle);
  if (!glfwVulkanSupported()) {
    LOG_ERROR("Vulkan: GLFW reports no Vulkan loader/ICD");
    return;
  }
  m_core = std::make_shared<VulkanCore>();
  if (!createInstance())
    return;
  if (glfwCreateWindowSurface(m_core->instance, m_window, nullptr, &m_surface) !=
      VK_SUCCESS) {
    LOG_ERROR("Vulkan: could not create a window surface");
    return;
  }
  if (!pickPhysicalDevice() || !createDevice())
    return;
  createFrameObjects();
  m_timer.init(*m_core);
  if (!createSwapchain())
    return;

  // Fallbacks for a sampler/block that nothing was bound to.
  const unsigned char white[4] = {255, 255, 255, 255};
  m_whiteTexture = std::make_unique<VulkanTexture>(this, m_core);
  m_whiteTexture->create(1, 1, api::TextureFormat::RGBA8, white,
                         api::TextureDesc{});
  m_dummyBuffer = std::make_unique<VulkanBuffer>(m_core);
  std::vector<unsigned char> zeros(kDummyBufferBytes, 0);
  m_dummyBuffer->setData(zeros.data(), zeros.size(), api::IBuffer::Usage::Static);

  LOG_INFO("Vulkan backend ready (%ux%u)", m_extent.width, m_extent.height);
}

void VulkanRenderDevice::shutdown() {
  if (!m_core || m_core->device == VK_NULL_HANDLE)
    return;
  VkDevice dev = m_core->device;
  vkDeviceWaitIdle(dev);
  if (m_frameActive) {
    vkResetCommandBuffer(m_cmd, 0);
    m_frameActive = false;
    m_rendering = false;
  }
  m_core->pollCompleted();
  m_timer.stopRecording();
  m_timer.shutdown();
  m_shader = nullptr;
  m_mesh = nullptr;
  m_target = nullptr;
  m_whiteTexture.reset();
  m_dummyBuffer.reset();
  for (auto &entry : m_pipelines)
    vkDestroyPipeline(dev, entry.second, nullptr);
  m_pipelines.clear();
  for (FrameSlot &fs : m_slots) {
    for (VkDescriptorPool pool : fs.pools)
      vkDestroyDescriptorPool(dev, pool, nullptr);
    fs.pools.clear();
    for (auto &chunk : fs.uniformChunks)
      m_core->destroyAllocation(chunk);
    fs.uniformChunks.clear();
    if (fs.imageAvailable)
      vkDestroySemaphore(dev, fs.imageAvailable, nullptr);
    fs.imageAvailable = VK_NULL_HANDLE;
  }
  destroySwapchain(/*keepHandle=*/false);
  if (m_surface) {
    vkDestroySurfaceKHR(m_core->instance, m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
  }
  m_core->flushAllDeferred(); // the device is idle
  m_shutDown = true;
}

// Frame

bool VulkanRenderDevice::ensureFrame() {
  if (m_frameActive)
    return true;
  if (m_shutDown || !m_core || !m_swapchain)
    return false;

  int w = 0, h = 0;
  glfwGetFramebufferSize(m_window, &w, &h);
  if (w == 0 || h == 0)
    return false; // minimised
  if (m_swapDirty || static_cast<uint32_t>(w) != m_extent.width ||
      static_cast<uint32_t>(h) != m_extent.height) {
    if (!createSwapchain())
      return false;
  }

  // Reusing this slot needs its previous frame (kFramesInFlight ago) finished;
  // the frame in between may still run, which is the overlap.
  FrameSlot &fs = slot();
  m_core->waitSerial(fs.serial);
  pollCompletedFrames();

  VkResult r = VK_ERROR_OUT_OF_DATE_KHR;
  for (int attempt = 0; attempt < 2 && r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR;
       ++attempt) {
    r = vkAcquireNextImageKHR(m_core->device, m_swapchain, UINT64_MAX,
                              fs.imageAvailable, VK_NULL_HANDLE, &m_imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR && !createSwapchain())
      return false;
  }
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
    return false;

  for (VkDescriptorPool pool : fs.pools)
    vkResetDescriptorPool(m_core->device, pool, 0);
  fs.poolIndex = 0;
  fs.uniformChunk = 0;
  fs.uniformOffset = 0;

  m_cmd = fs.cmd;
  vkResetCommandBuffer(m_cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(m_cmd, &bi);
  m_timer.startRecording(m_cmd, m_slot);

  transitionImage(m_cmd, m_images[m_imageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 1,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  // This slot's depth image was last used kFramesInFlight frames ago and that
  // frame has finished (waitSerial above), so no barrier dependency is needed.
  transitionImage(m_cmd, m_depth[m_slot].image.image, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 0, 1,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  0,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT);

  m_frameActive = true;
  m_rendering = false;
  m_colorInitialised = false;
  m_depthInitialised = false;
  return true;
}

VkExtent2D VulkanRenderDevice::currentExtent() const {
  return m_target ? VkExtent2D{m_target->width(), m_target->height()}
                  : m_extent;
}

void VulkanRenderDevice::beginPass(bool colorOnly) {
  VulkanRenderTarget::Slot *target =
      m_target ? &m_target->slot(m_slot) : nullptr;
  VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = target ? target->colorView : m_imageViews[m_imageIndex];
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  const bool clearColor = (m_clearPending && !colorOnly) || !m_colorInitialised;
  color.loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  const bool useClearColor = m_clearPending && !colorOnly;
  for (int i = 0; i < 4; ++i)
    color.clearValue.color.float32[i] = useClearColor ? m_clearColor[i] : (i == 3 ? 1.0f : 0.0f);

  VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth.imageView = target ? target->depthView : m_depth[m_slot].view;
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp = (m_clearPending || !m_depthInitialised)
                     ? VK_ATTACHMENT_LOAD_OP_CLEAR
                     : VK_ATTACHMENT_LOAD_OP_LOAD;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth.clearValue.depthStencil = {1.0f, 0};

  VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
  ri.renderArea = {{0, 0}, currentExtent()};
  ri.layerCount = 1;
  ri.colorAttachmentCount = 1;
  ri.pColorAttachments = &color;
  ri.pDepthAttachment = colorOnly ? nullptr : &depth;
  vkCmdBeginRendering(m_cmd, &ri);

  m_colorInitialised = true;
  if (!colorOnly) {
    m_depthInitialised = true;
    m_clearPending = false;
  }
  m_rendering = true;
}

bool VulkanRenderDevice::ensureRendering() {
  if (!ensureFrame())
    return false;
  if (!m_rendering)
    beginPass(/*colorOnly=*/false);
  return true;
}

void VulkanRenderDevice::endRendering() {
  if (m_rendering) {
    vkCmdEndRendering(m_cmd);
    m_rendering = false;
  }
}

VkCommandBuffer VulkanRenderDevice::beginTransfer(size_t stagingBytes) {
  // Staging stays alive until the frame it rode in has finished; past this much
  // a blocking upload (which frees it at once) is the better trade.
  constexpr size_t kMaxQueuedStaging = 256u << 20;
  if (m_stagingBytes + stagingBytes > kMaxQueuedStaging || !ensureFrame())
    return VK_NULL_HANDLE;
  endRendering(); // transfers cannot be recorded inside a render pass
  m_stagingBytes += stagingBytes;
  return m_cmd;
}

void VulkanRenderDevice::setViewport(int x, int y, int w, int h) {
  m_viewport[0] = x;
  m_viewport[1] = y;
  m_viewport[2] = w;
  m_viewport[3] = h;
  m_viewportSet = true;
}

void VulkanRenderDevice::setScissor(int x, int y, int w, int h) {
  m_scissor[0] = x;
  m_scissor[1] = y;
  m_scissor[2] = w;
  m_scissor[3] = h;
  m_scissorSet = true;
}

api::Extent VulkanRenderDevice::viewportExtent() const {
  if (m_viewportSet && m_viewport[2] > 0 && m_viewport[3] > 0)
    return {static_cast<uint32_t>(m_viewport[2]),
            static_cast<uint32_t>(m_viewport[3])};
  return {m_extent.width, m_extent.height};
}

void VulkanRenderDevice::recordViewportAndScissor() {
  if (m_target) {
    // Not flipped: row 0 of the image is the bottom of the picture, as in a GL
    // framebuffer texture, so a shader samples it like any other texture.
    const float w = static_cast<float>(m_target->width());
    const float h = static_cast<float>(m_target->height());
    const VkViewport vp{0.0f, 0.0f, w, h, 0.0f, 1.0f};
    vkCmdSetViewport(m_cmd, 0, 1, &vp);
    const VkRect2D rect{{0, 0}, {m_target->width(), m_target->height()}};
    vkCmdSetScissor(m_cmd, 0, 1, &rect);
    return;
  }
  const float fbh = static_cast<float>(m_extent.height);
  float x = 0, y = 0, w = static_cast<float>(m_extent.width), h = fbh;
  if (m_viewportSet) {
    x = static_cast<float>(m_viewport[0]);
    y = static_cast<float>(m_viewport[1]);
    w = static_cast<float>(m_viewport[2]);
    h = static_cast<float>(m_viewport[3]);
  }
  // GL's origin is bottom-left; Vulkan's is top-left. A negative height keeps
  // GL's up direction and winding.
  VkViewport vp{x, fbh - y, w, -h, 0.0f, 1.0f};
  vkCmdSetViewport(m_cmd, 0, 1, &vp);

  int sx = 0, sy = 0, sw = static_cast<int>(m_extent.width),
      sh = static_cast<int>(m_extent.height);
  if (m_scissorSet) {
    sx = m_scissor[0];
    sy = m_scissor[1];
    sw = m_scissor[2];
    sh = m_scissor[3];
  }
  const int top = std::max(0, static_cast<int>(m_extent.height) - (sy + sh));
  VkRect2D rect{{std::max(0, sx), top},
                {static_cast<uint32_t>(std::max(0, sw)),
                 static_cast<uint32_t>(std::max(0, sh))}};
  vkCmdSetScissor(m_cmd, 0, 1, &rect);
}

void VulkanRenderDevice::clear(float r, float g, float b, float a) {
  if (!ensureFrame())
    return;
  // Whatever was drawn so far is final; the next draw starts a pass that
  // clears colour and depth.
  endRendering();
  m_clearColor[0] = r;
  m_clearColor[1] = g;
  m_clearColor[2] = b;
  m_clearColor[3] = a;
  m_clearPending = true;
}

void VulkanRenderDevice::setRenderTarget(api::IRenderTarget *target) {
  auto *next = static_cast<VulkanRenderTarget *>(target);
  if (next && !next->valid())
    next = nullptr;
  if (next == m_target)
    return;
  if (!ensureFrame()) {
    m_target = next;
    return;
  }
  endRendering();

  if (m_target) {
    // Done drawing: what was drawn is sampled from here on.
    VulkanRenderTarget::Slot &s = m_target->slot(m_slot);
    s.colorInitialised = m_colorInitialised;
    s.depthInitialised = m_depthInitialised;
    transitionImage(m_cmd, s.color.image, s.colorLayout,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT);
    s.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  } else {
    m_windowColorInitialised = m_colorInitialised;
    m_windowDepthInitialised = m_depthInitialised;
  }

  m_target = next;

  if (!m_target) {
    m_colorInitialised = m_windowColorInitialised;
    m_depthInitialised = m_windowDepthInitialised;
    return;
  }

  VulkanRenderTarget::Slot &s = m_target->slot(m_slot);
  const uint64_t serial = m_core->recordingSerial;
  if (s.frameSerial != serial) {
    // First use this frame. The frame that last used this slot has finished
    // (ensureFrame waited for it), so nothing needs to be waited for.
    s.frameSerial = serial;
    s.colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    s.colorInitialised = false;
    s.depthInitialised = false;
    transitionImage(m_cmd, s.depth.image, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 0, 1,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    0,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
  }
  if (s.colorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    const bool fresh = s.colorLayout == VK_IMAGE_LAYOUT_UNDEFINED;
    transitionImage(m_cmd, s.color.image, s.colorLayout,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 1,
                    fresh ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                          : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    fresh ? 0 : VK_ACCESS_2_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    s.colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  m_colorInitialised = s.colorInitialised;
  m_depthInitialised = s.depthInitialised;
}

std::unique_ptr<api::IRenderTarget>
VulkanRenderDevice::createRenderTarget(const api::RenderTargetDesc &desc) {
  return std::make_unique<VulkanRenderTarget>(this, m_core, desc, m_swapFormat,
                                              m_depthFormat);
}

void VulkanRenderDevice::present(void * /*windowHandle*/) {
  setRenderTarget(nullptr);
  if (!m_frameActive && !ensureFrame())
    return;
  ensureRendering(); // guarantees the image was cleared even if nothing drew
  endRendering();

  transitionImage(m_cmd, m_images[m_imageIndex],
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, 1,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
  m_timer.stopRecording();
  vkEndCommandBuffer(m_cmd);

  FrameSlot &fs = slot();
  const uint64_t serial = m_core->recordingSerial;
  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  // Binary semaphores (acquire, render-done) take a dummy value 0 in the
  // timeline-submit arrays; the timeline gets the frame's serial.
  const uint64_t waitValue = 0;
  const uint64_t signalValues[2] = {0, serial};
  VkTimelineSemaphoreSubmitInfo ts{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
  ts.waitSemaphoreValueCount = 1;
  ts.pWaitSemaphoreValues = &waitValue;
  ts.signalSemaphoreValueCount = 2;
  ts.pSignalSemaphoreValues = signalValues;
  const VkSemaphore signals[2] = {m_renderDone[m_imageIndex], m_core->timeline};
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.pNext = &ts;
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &fs.imageAvailable;
  si.pWaitDstStageMask = &waitStage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &m_cmd;
  si.signalSemaphoreCount = 2;
  si.pSignalSemaphores = signals;
  vkQueueSubmit(m_core->queue, 1, &si, VK_NULL_HANDLE);
  fs.serial = serial;
  ++m_core->recordingSerial;
  m_stagingBytes = 0;

  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &m_renderDone[m_imageIndex];
  pi.swapchainCount = 1;
  pi.pSwapchains = &m_swapchain;
  pi.pImageIndices = &m_imageIndex;
  const VkResult pr = vkQueuePresentKHR(m_core->queue, &pi);
  if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR)
    m_swapDirty = true;

  // No wait here: the GPU runs this frame while the CPU records the next.
  m_frameActive = false;
  m_slot = (m_slot + 1) % kFramesInFlight;
  pollCompletedFrames();
}

void VulkanRenderDevice::pollCompletedFrames() {
  m_core->pollCompleted();
  m_core->flushDeferred();
  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    if (m_slots[i].serial != 0 && m_slots[i].serial <= m_core->completedSerial &&
        m_timer.hasPending(i))
      m_timer.collect(i);
  }
}

// State the engine sets between draws

void VulkanRenderDevice::bindUniformBuffer(uint32_t binding, api::IBuffer &buffer) {
  if (binding < api::kUniformBindingLimit)
    m_uniformBuffers[binding] = static_cast<VulkanBuffer *>(&buffer);
}

void VulkanRenderDevice::bindBufferTexture(uint32_t unit, api::IBuffer &buffer,
                                           api::BufferTextureFormat format) {
  if (unit < api::kTextureUnitLimit)
    m_unitBuffer[unit] = {static_cast<VulkanBuffer *>(&buffer), format};
}

void VulkanRenderDevice::setupInstanceAttributes(api::IBuffer &instanceBuffer,
                                                 const api::VertexLayout &layout) {
  m_instanceBuffer = static_cast<VulkanBuffer *>(&instanceBuffer);
  m_instanceLayout = layout;
}

void VulkanRenderDevice::unbindInstanceAttributes(const api::VertexLayout &) {
  m_instanceBuffer = nullptr;
}

std::unique_ptr<api::IShader> VulkanRenderDevice::createShader() {
  return std::make_unique<VulkanShader>(this, m_core, m_nextShaderId++);
}
std::unique_ptr<api::ITexture> VulkanRenderDevice::createTexture() {
  return std::make_unique<VulkanTexture>(this, m_core);
}
std::unique_ptr<api::IMesh> VulkanRenderDevice::createMesh() {
  return std::make_unique<VulkanMesh>(this, m_core);
}
std::unique_ptr<api::IBuffer> VulkanRenderDevice::createBuffer() {
  return std::make_unique<VulkanBuffer>(m_core);
}

// Drawing

VkPipeline VulkanRenderDevice::pipelineFor(const VulkanShader &shader,
                                           const VulkanMesh *mesh, bool instanced) {
  std::string key;
  keyAdd(key, shader.id());
  keyAdd(key, static_cast<uint8_t>(m_state.blend));
  keyAdd(key, m_state.colorWrite);
  if (mesh) {
    const api::VertexLayout &l = mesh->layout();
    keyAdd(key, l.stride);
    for (const auto &a : l.attributes) {
      keyAdd(key, a.offset);
      keyAdd(key, a.componentCount);
    }
  }
  keyAdd(key, instanced);
  if (instanced) {
    keyAdd(key, m_instanceLayout.stride);
    for (const auto &a : m_instanceLayout.attributes) {
      keyAdd(key, a.offset);
      keyAdd(key, a.componentCount);
    }
  }
  auto found = m_pipelines.find(key);
  if (found != m_pipelines.end())
    return found->second;

  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
  if (mesh) {
    bindings.push_back({0, mesh->layout().stride, VK_VERTEX_INPUT_RATE_VERTEX});
    for (size_t i = 0; i < mesh->layout().attributes.size(); ++i) {
      const auto &a = mesh->layout().attributes[i];
      attributes.push_back({static_cast<uint32_t>(i), 0,
                            attributeFormat(a.componentCount), a.offset});
    }
  }
  if (instanced) {
    bindings.push_back({1, m_instanceLayout.stride, VK_VERTEX_INPUT_RATE_INSTANCE});
    for (size_t i = 0; i < m_instanceLayout.attributes.size(); ++i) {
      const auto &a = m_instanceLayout.attributes[i];
      attributes.push_back({api::kFirstInstanceAttribLocation + static_cast<uint32_t>(i),
                            1, attributeFormat(a.componentCount), a.offset});
    }
  }
  VkPipelineVertexInputStateCreateInfo vertexInput{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
  vertexInput.pVertexBindingDescriptions = bindings.data();
  vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
  vertexInput.pVertexAttributeDescriptions = attributes.data();

  VkPipelineInputAssemblyStateCreateInfo assembly{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo msaa{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = m_state.colorWrite
                             ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
                             : 0;
  if (m_state.blend != api::BlendMode::None) {
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = blend.dstAlphaBlendFactor =
        m_state.blend == api::BlendMode::Alpha ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
                                               : VK_BLEND_FACTOR_ONE;
    blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  VkPipelineColorBlendStateCreateInfo blendState{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blendState.attachmentCount = 1;
  blendState.pAttachments = &blend;

  const VkDynamicState dynamics[] = {
      VK_DYNAMIC_STATE_VIEWPORT,           VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_CULL_MODE,          VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
      VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
      VK_DYNAMIC_STATE_FRONT_FACE};
  VkPipelineDynamicStateCreateInfo dynamic{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = sizeof(dynamics) / sizeof(dynamics[0]);
  dynamic.pDynamicStates = dynamics;

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = shader.vertexModule();
  stages[0].pName = "main";
  stages[1] = stages[0];
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = shader.fragmentModule();

  VkPipelineRenderingCreateInfo rendering{
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &m_swapFormat;
  rendering.depthAttachmentFormat = m_depthFormat;

  VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pi.pNext = &rendering;
  pi.stageCount = 2;
  pi.pStages = stages;
  pi.pVertexInputState = &vertexInput;
  pi.pInputAssemblyState = &assembly;
  pi.pViewportState = &viewport;
  pi.pRasterizationState = &raster;
  pi.pMultisampleState = &msaa;
  pi.pDepthStencilState = &depth;
  pi.pColorBlendState = &blendState;
  pi.pDynamicState = &dynamic;
  pi.layout = shader.pipelineLayout();

  VkPipeline pipeline = VK_NULL_HANDLE;
  if (vkCreateGraphicsPipelines(m_core->device, VK_NULL_HANDLE, 1, &pi, nullptr,
                                &pipeline) != VK_SUCCESS) {
    LOG_ERROR("Vulkan: pipeline creation failed");
    pipeline = VK_NULL_HANDLE;
  }
  m_pipelines.emplace(std::move(key), pipeline);
  return pipeline;
}

VkDescriptorSet VulkanRenderDevice::allocateSet(VkDescriptorSetLayout layout) {
  for (;;) {
    FrameSlot &fs = slot();
    if (fs.poolIndex == fs.pools.size()) {
      const VkDescriptorPoolSize sizes[] = {
          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16384},
          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16384},
          {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 4096}};
      VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      ci.maxSets = 4096;
      ci.poolSizeCount = 3;
      ci.pPoolSizes = sizes;
      VkDescriptorPool pool = VK_NULL_HANDLE;
      if (vkCreateDescriptorPool(m_core->device, &ci, nullptr, &pool) != VK_SUCCESS)
        return VK_NULL_HANDLE;
      fs.pools.push_back(pool);
    }
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = fs.pools[fs.poolIndex];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult r = vkAllocateDescriptorSets(m_core->device, &ai, &set);
    if (r == VK_SUCCESS)
      return set;
    if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL)
      return VK_NULL_HANDLE;
    ++fs.poolIndex; // this pool is full: move to (or make) the next one
  }
}

bool VulkanRenderDevice::allocUniform(uint32_t size, VkBuffer &buffer,
                                      VkDeviceSize &offset, void *&mapped) {
  const VkDeviceSize align =
      std::max<VkDeviceSize>(m_core->props.limits.minUniformBufferOffsetAlignment, 16);
  FrameSlot &fs = slot();
  for (;;) {
    if (fs.uniformChunk == fs.uniformChunks.size()) {
      VulkanCore::Allocation chunk;
      if (!m_core->createMappedBuffer(std::max<VkDeviceSize>(kUniformChunkBytes, size),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, chunk))
        return false;
      fs.uniformChunks.push_back(chunk);
    }
    const VkDeviceSize aligned = (fs.uniformOffset + align - 1) / align * align;
    VulkanCore::Allocation &chunk = fs.uniformChunks[fs.uniformChunk];
    if (aligned + size <= chunk.size) {
      buffer = chunk.buffer;
      offset = aligned;
      mapped = static_cast<char *>(chunk.mapped) + aligned;
      fs.uniformOffset = aligned + size;
      return true;
    }
    ++fs.uniformChunk;
    fs.uniformOffset = 0;
  }
}

VkDescriptorSet VulkanRenderDevice::buildDescriptors(const VulkanShader &shader) {
  VkDescriptorSet set = allocateSet(shader.setLayout());
  if (set == VK_NULL_HANDLE)
    return VK_NULL_HANDLE;

  const size_t maxWrites =
      shader.blocks().size() + 2 + shader.samplers().size();
  std::vector<VkDescriptorBufferInfo> bufferInfos;
  std::vector<VkDescriptorImageInfo> imageInfos;
  std::vector<VkBufferView> texelViews;
  bufferInfos.reserve(maxWrites);
  imageInfos.reserve(maxWrites);
  texelViews.reserve(maxWrites);
  std::vector<VkWriteDescriptorSet> writes;
  writes.reserve(maxWrites);

  auto writeBuffer = [&](uint32_t binding, VkBuffer buffer, VkDeviceSize offset,
                         VkDeviceSize range) {
    bufferInfos.push_back({buffer, offset, range});
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set;
    w.dstBinding = binding;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &bufferInfos.back();
    writes.push_back(w);
  };

  const VkDeviceSize maxRange = m_core->props.limits.maxUniformBufferRange;
  for (const auto &b : shader.blocks()) {
    VulkanBuffer *buf = b.engineBinding < api::kUniformBindingLimit
                            ? m_uniformBuffers[b.engineBinding]
                            : nullptr;
    if (!buf || buf->handle() == VK_NULL_HANDLE || buf->size() == 0)
      buf = m_dummyBuffer.get();
    buf->markUsed(m_core->recordingSerial);
    writeBuffer(b.binding, buf->handle(), 0,
                std::min<VkDeviceSize>(buf->size(), maxRange));
  }

  for (int stage = 0; stage < 2; ++stage) {
    const auto &loose = shader.loose(stage);
    if (loose.binding == UINT32_MAX)
      continue;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    void *mapped = nullptr;
    if (!allocUniform(loose.size, buffer, offset, mapped))
      return VK_NULL_HANDLE;
    std::memcpy(mapped, loose.data.data(), loose.size);
    writeBuffer(loose.binding, buffer, offset, loose.size);
  }

  for (const auto &s : shader.samplers()) {
    const uint32_t unit =
        std::min<uint32_t>(static_cast<uint32_t>(std::max(0, s.unit)),
                           api::kTextureUnitLimit - 1);
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set;
    w.dstBinding = s.binding;
    w.descriptorCount = 1;
    if (s.texelBuffer) {
      VulkanBuffer *buf = m_unitBuffer[unit].buffer;
      api::BufferTextureFormat format = m_unitBuffer[unit].format;
      if (!buf || buf->handle() == VK_NULL_HANDLE) {
        buf = m_dummyBuffer.get();
        format = api::BufferTextureFormat::RGBA32F;
      }
      buf->markUsed(m_core->recordingSerial);
      texelViews.push_back(buf->view(format));
      w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
      w.pTexelBufferView = &texelViews.back();
    } else {
      const VulkanTexture *tex = m_unitTexture[unit];
      if (!tex || tex->view() == VK_NULL_HANDLE)
        tex = m_whiteTexture.get();
      imageInfos.push_back(
          {tex->sampler(), tex->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
      w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w.pImageInfo = &imageInfos.back();
    }
    writes.push_back(w);
  }

  vkUpdateDescriptorSets(m_core->device, static_cast<uint32_t>(writes.size()),
                         writes.data(), 0, nullptr);
  return set;
}

bool VulkanRenderDevice::draw(bool indexed, uint32_t count, uint32_t instanceCount) {
  const VulkanShader *shader = m_shader;
  if (!shader || !shader->valid())
    return false;
  const VulkanMesh *mesh = indexed ? m_mesh : nullptr;
  if (indexed && (!mesh || !mesh->vertexBuffer() || !mesh->indexBuffer() ||
                  mesh->vertexBuffer()->handle() == VK_NULL_HANDLE))
    return false;
  if (!ensureRendering())
    return false;

  const bool instanced = indexed && m_instanceBuffer &&
                         m_instanceBuffer->handle() != VK_NULL_HANDLE &&
                         !m_instanceLayout.attributes.empty();
  VkPipeline pipeline = pipelineFor(*shader, mesh, instanced);
  if (pipeline == VK_NULL_HANDLE)
    return false;
  VkDescriptorSet set = buildDescriptors(*shader);
  if (set == VK_NULL_HANDLE)
    return false;

  vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          shader->pipelineLayout(), 0, 1, &set, 0, nullptr);

  recordViewportAndScissor();
  VkCullModeFlags cull = VK_CULL_MODE_NONE;
  if (m_state.cull == api::CullMode::Back)
    cull = VK_CULL_MODE_BACK_BIT;
  else if (m_state.cull == api::CullMode::Front)
    cull = VK_CULL_MODE_FRONT_BIT;
  vkCmdSetCullMode(m_cmd, cull);
  // A render target is drawn unflipped (see recordViewportAndScissor), which
  // mirrors the winding.
  vkCmdSetFrontFace(m_cmd, m_target ? VK_FRONT_FACE_CLOCKWISE
                                    : VK_FRONT_FACE_COUNTER_CLOCKWISE);
  vkCmdSetDepthTestEnable(m_cmd, m_state.depthTest ? VK_TRUE : VK_FALSE);
  vkCmdSetDepthWriteEnable(m_cmd, m_state.depthWrite ? VK_TRUE : VK_FALSE);
  VkCompareOp op = VK_COMPARE_OP_LESS;
  if (m_state.depthFunc == api::DepthFunc::LessEqual)
    op = VK_COMPARE_OP_LESS_OR_EQUAL;
  else if (m_state.depthFunc == api::DepthFunc::Equal)
    op = VK_COMPARE_OP_EQUAL;
  vkCmdSetDepthCompareOp(m_cmd, op);

  if (!indexed) {
    vkCmdDraw(m_cmd, count, instanceCount, 0, 0);
    return true;
  }
  const uint64_t serial = m_core->recordingSerial;
  mesh->vertexBuffer()->markUsed(serial);
  mesh->indexBuffer()->markUsed(serial);
  if (instanced)
    m_instanceBuffer->markUsed(serial);
  VkBuffer buffers[2] = {mesh->vertexBuffer()->handle(),
                         instanced ? m_instanceBuffer->handle() : VK_NULL_HANDLE};
  const VkDeviceSize offsets[2] = {0, 0};
  vkCmdBindVertexBuffers(m_cmd, 0, instanced ? 2 : 1, buffers, offsets);
  vkCmdBindIndexBuffer(m_cmd, mesh->indexBuffer()->handle(), 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(m_cmd, count, instanceCount, 0, 0, 0);
  return true;
}

void VulkanRenderDevice::drawFullscreenTriangle() {
  draw(/*indexed=*/false, 3, 1);
}

void VulkanRenderDevice::drawIndexedInstanced(size_t indexCount,
                                              uint32_t instanceCount) {
  draw(/*indexed=*/true, static_cast<uint32_t>(indexCount), instanceCount);
}

// Dear ImGui

void VulkanRenderDevice::initImGui(void *windowHandle) {
  ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow *>(windowHandle), true);

  ImGui_ImplVulkan_InitInfo info = {};
  info.ApiVersion = VK_API_VERSION_1_3;
  info.Instance = m_core->instance;
  info.PhysicalDevice = m_core->physical;
  info.Device = m_core->device;
  info.QueueFamily = m_core->queueFamily;
  info.Queue = m_core->queue;
  info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE * 4;
  info.MinImageCount = std::max<uint32_t>(2, static_cast<uint32_t>(m_images.size()));
  info.ImageCount = std::max<uint32_t>(info.MinImageCount, static_cast<uint32_t>(m_images.size()));
  info.UseDynamicRendering = true;
  info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  info.PipelineInfoMain.PipelineRenderingCreateInfo = {
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
  info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats =
      &m_swapFormat;
  ImGui_ImplVulkan_Init(&info);
  m_imguiReady = true;
}

void VulkanRenderDevice::imguiNewFrame() {
  if (m_imguiReady)
    ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
}

void VulkanRenderDevice::imguiRender() {
  ImDrawData *data = ImGui::GetDrawData();
  if (!m_imguiReady || !data || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0)
    return;
  if (!ensureFrame())
    return;
  // The UI goes on top of the finished scene in a colour-only pass.
  setRenderTarget(nullptr);
  endRendering();
  beginPass(/*colorOnly=*/true);
  ImGui_ImplVulkan_RenderDrawData(data, m_cmd);
  endRendering();
}

void VulkanRenderDevice::shutdownImGui() {
  if (!m_imguiReady)
    return;
  vkDeviceWaitIdle(m_core->device);
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  m_imguiReady = false;
}

} // namespace componeng::renderer::vulkan
