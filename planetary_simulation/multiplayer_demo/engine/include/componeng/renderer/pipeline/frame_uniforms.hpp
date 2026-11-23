#pragma once

#include "componeng/core/types.hpp"
#include "componeng/ecs/entity.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/frame_data.hpp"

#include <memory>

namespace componeng::ecs {
class World;
}

namespace componeng::renderer {

/**
 * @brief The values that are constant for a whole frame (camera, light, fog,
 * time): gathered from the world once, uploaded to one uniform buffer, and
 * bound once, for every shader that declares the FrameData block. A shader
 * that does not declare it gets nothing (Shader::compile warns if it reads
 * the values as loose uniforms).
 */
class FrameUniforms {
public:
  /** Gathers this frame's values from `world` (the main camera's matrices,
   *  the directional light, fog, time), uploads them and binds the buffer at
   *  kFrameDataBinding. */
  void update(ecs::World &world, ecs::EntityID camera);

  const FrameData &data() const {
    return m_data;
  }

private:
  FrameData m_data{};
  std::unique_ptr<api::IBuffer> m_buffer;
};

} // namespace componeng::renderer
