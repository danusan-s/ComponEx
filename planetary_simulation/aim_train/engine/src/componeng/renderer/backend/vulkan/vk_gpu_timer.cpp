#include "componeng/renderer/backend/vulkan/vk_gpu_timer.hpp"

#include <vector>

namespace componeng::renderer::vulkan {

void VulkanGpuTimer::init(const VulkanCore &core) {
  m_core = &core;

  uint32_t n = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(core.physical, &n, nullptr);
  std::vector<VkQueueFamilyProperties> families(n);
  vkGetPhysicalDeviceQueueFamilyProperties(core.physical, &n, families.data());
  const uint32_t bits = families.at(core.queueFamily).timestampValidBits;
  if (bits == 0 || core.props.limits.timestampPeriod <= 0.0f)
    return; // no timestamps on this queue: timing stays unavailable

  m_validMask = bits >= 64 ? ~0ull : ((1ull << bits) - 1);
  m_nsPerTick = core.props.limits.timestampPeriod;

  VkQueryPoolCreateInfo ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
  ci.queryCount = kMaxQueries * kFramesInFlight;
  if (vkCreateQueryPool(core.device, &ci, nullptr, &m_pool) != VK_SUCCESS)
    m_pool = VK_NULL_HANDLE;
}

void VulkanGpuTimer::shutdown() {
  if (m_pool != VK_NULL_HANDLE && m_core)
    vkDestroyQueryPool(m_core->device, m_pool, nullptr);
  m_pool = VK_NULL_HANDLE;
  m_cmd = VK_NULL_HANDLE;
}

uint32_t VulkanGpuTimer::stamp() {
  SlotData &slot = m_slots[m_slot];
  if (!m_cmd || slot.used >= kMaxQueries)
    return kNone;
  const uint32_t q = slot.used++; // index within the slot's range
  vkCmdWriteTimestamp2(m_cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_pool,
                       m_slot * kMaxQueries + q);
  return q;
}

void VulkanGpuTimer::startRecording(VkCommandBuffer cmd, uint32_t slot) {
  if (!supported())
    return;
  m_cmd = cmd;
  m_slot = slot;
  m_slots[slot] = SlotData{};
  m_openFrame = kNone;
  m_openPass.fill(kNone);
  m_openShader.clear();
  vkCmdResetQueryPool(cmd, m_pool, slot * kMaxQueries, kMaxQueries);
  if (m_framePending) {
    m_openFrame = stamp();
    m_framePending = false;
  }
}

void VulkanGpuTimer::stopRecording() {
  if (m_cmd)
    m_slots[m_slot].pending = m_slots[m_slot].used > 0;
  m_cmd = VK_NULL_HANDLE;
  m_framePending = false;
  m_openFrame = kNone;
  m_openPass.fill(kNone);
  m_openShader.clear();
}

void VulkanGpuTimer::beginFrameSpan() {
  if (!supported())
    return;
  if (m_cmd)
    m_openFrame = stamp();
  else
    m_framePending = true;
}

void VulkanGpuTimer::endFrameSpan() {
  m_framePending = false;
  if (m_openFrame == kNone)
    return;
  const uint32_t end = stamp();
  if (end != kNone)
    m_slots[m_slot].spans.push_back({Kind::Frame, 0, 0, m_openFrame, end});
  m_openFrame = kNone;
}

void VulkanGpuTimer::beginPass(api::PassId pass) {
  if (!m_cmd)
    return;
  m_openPass[static_cast<std::size_t>(pass)] = stamp();
}

void VulkanGpuTimer::endPass(api::PassId pass) {
  uint32_t &open = m_openPass[static_cast<std::size_t>(pass)];
  if (open == kNone)
    return;
  const uint32_t end = stamp();
  if (end != kNone)
    m_slots[m_slot].spans.push_back(
        {Kind::Pass, static_cast<uint32_t>(pass), 0, open, end});
  open = kNone;
}

void VulkanGpuTimer::beginShader(uint64_t shaderId) {
  if (!m_cmd || m_openShader.count(shaderId))
    return; // spans do not nest within one shader
  const uint32_t q = stamp();
  if (q != kNone)
    m_openShader[shaderId] = q;
}

void VulkanGpuTimer::endShader(uint64_t shaderId) {
  auto it = m_openShader.find(shaderId);
  if (it == m_openShader.end())
    return;
  const uint32_t end = stamp();
  if (end != kNone)
    m_slots[m_slot].spans.push_back(
        {Kind::Shader, 0, shaderId, it->second, end});
  m_openShader.erase(it);
}

void VulkanGpuTimer::collect(uint32_t slotIndex) {
  SlotData &slot = m_slots[slotIndex];
  if (!supported() || !slot.pending)
    return;
  slot.pending = false;

  std::vector<uint64_t> ticks(slot.used);
  // The frame has finished, so written queries are available; one never
  // written (an unclosed span) is in no closed span, so NOT_READY is harmless.
  vkGetQueryPoolResults(m_core->device, m_pool, slotIndex * kMaxQueries,
                        slot.used, ticks.size() * sizeof(uint64_t),
                        ticks.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

  auto ms = [&](const Span &s) {
    const uint64_t delta = (ticks[s.end] - ticks[s.begin]) & m_validMask;
    return static_cast<double>(delta) * m_nsPerTick * 1e-6;
  };

  for (auto &entry : m_shaderMs)
    entry.second = 0.0; // shaders that did not run this frame read as 0

  std::array<bool, api::kPassCount> passSeen{};
  double frame = 0.0;
  bool frameSeen = false;
  for (const Span &s : slot.spans) {
    switch (s.kind) {
      case Kind::Frame:
        frame += ms(s);
        frameSeen = true;
        break;
      case Kind::Pass:
        if (!passSeen[s.pass]) {
          m_passMs[s.pass] = 0.0;
          passSeen[s.pass] = true;
        }
        m_passMs[s.pass] += ms(s);
        break;
      case Kind::Shader:
        m_shaderMs[s.id] += ms(s);
        break;
    }
  }
  if (frameSeen)
    m_frameMs = frame;
  m_haveResult = true;

  slot.used = 0;
  slot.spans.clear();
}

double VulkanGpuTimer::shaderMs(uint64_t shaderId) const {
  if (!m_haveResult)
    return -1.0;
  auto it = m_shaderMs.find(shaderId);
  return it == m_shaderMs.end() ? 0.0 : it->second;
}

} // namespace componeng::renderer::vulkan
