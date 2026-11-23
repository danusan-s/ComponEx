#pragma once

#include "componeng/renderer/backend/api/pass_id.hpp"
#include "componeng/renderer/backend/vulkan/vk_core.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace componeng::renderer::vulkan {

/**
 * GPU timing for the Vulkan backend (whole frame, per render pass, per
 * shader) with timestamp queries. Each frame slot has its own query range;
 * the device calls collect(slot) once that slot's frame has finished, so
 * results trail the CPU by about a frame. Everything is a no-op, and the
 * accessors report "unavailable" (negative), without timestamp support.
 */
class VulkanGpuTimer {
public:
  void init(const VulkanCore &core);
  void shutdown();
  bool supported() const {
    return m_pool != VK_NULL_HANDLE;
  }

  /** `slot`'s command buffer is open: reset its queries and accept spans. */
  void startRecording(VkCommandBuffer cmd, uint32_t slot);
  /** About to end the command buffer; spans still open are discarded. */
  void stopRecording();

  /** May be called before the command buffer exists (the engine requests it
   *  ahead of the first system); the start is stamped once recording begins. */
  void beginFrameSpan();
  void endFrameSpan();
  void beginPass(api::PassId pass);
  void endPass(api::PassId pass);
  void beginShader(uint64_t shaderId);
  void endShader(uint64_t shaderId);

  /** True if `slot` holds a submitted frame whose results are not read yet. */
  bool hasPending(uint32_t slot) const {
    return supported() && m_slots[slot].pending;
  }
  /** Read slot `slot`'s queries. Only call once its frame has finished. */
  void collect(uint32_t slot);

  double frameMs() const {
    return m_frameMs;
  }
  double passMs(api::PassId pass) const {
    return m_passMs[static_cast<std::size_t>(pass)];
  }
  /** 0 if the shader did not run in the last completed frame, negative if no
   *  frame has completed yet. */
  double shaderMs(uint64_t shaderId) const;

private:
  static constexpr uint32_t kMaxQueries = 4096;
  static constexpr uint32_t kNone = UINT32_MAX;

  enum class Kind : uint8_t { Frame, Pass, Shader };
  struct Span {
    Kind kind;
    uint32_t pass; // Kind::Pass
    uint64_t id;   // Kind::Shader
    uint32_t begin;
    uint32_t end;
  };

  uint32_t stamp();

  const VulkanCore *m_core = nullptr;
  VkQueryPool m_pool = VK_NULL_HANDLE;
  VkCommandBuffer m_cmd = VK_NULL_HANDLE; // non-null while recording
  uint64_t m_validMask = ~0ull;
  double m_nsPerTick = 0.0;

  struct SlotData {
    uint32_t used = 0;
    std::vector<Span> spans; // closed spans of this slot's frame
    bool pending = false;    // submitted, results not collected yet
  };
  std::array<SlotData, kFramesInFlight> m_slots;
  uint32_t m_slot = 0; // slot being recorded

  bool m_framePending = false;
  uint32_t m_openFrame = kNone;
  std::array<uint32_t, api::kPassCount> m_openPass;
  std::unordered_map<uint64_t, uint32_t> m_openShader;

  double m_frameMs = -1.0;
  std::array<double, api::kPassCount> m_passMs;
  std::unordered_map<uint64_t, double> m_shaderMs;
  bool m_haveResult = false;

public:
  VulkanGpuTimer() {
    m_openPass.fill(kNone);
    m_passMs.fill(-1.0);
  }
};

} // namespace componeng::renderer::vulkan
