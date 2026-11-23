#include "componeng/renderer/pipeline/frame_uniforms.hpp"

#include "componeng/camera/camera_component.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/renderer/asset/shader.hpp"
#include "componeng/renderer/component/light_component.hpp"
#include "componeng/resources/fog.hpp"

#include <algorithm>

namespace componeng::renderer {

namespace {

// Used when the scene has no DirectionalLightComponent, and for the ambient
// term, which has no component of its own yet.
constexpr core::Vec3 kDefaultLightDir = core::Vec3(-0.2f, 1.0f, -0.3f);
constexpr core::Vec3 kDefaultLightColor = core::Vec3(1.0f, 1.0f, 1.0f);
constexpr core::Vec3 kAmbientColor = core::Vec3(0.3f, 0.4f, 0.6f);
constexpr float kAmbientStrength = 0.4f;

} // namespace

void FrameUniforms::update(ecs::World &world, ecs::EntityID camera) {
  const core::Vec3 cameraPos =
      world.readComponent<core::TransformComponent>(camera).position;
  const core::Mat4 &viewProj =
      world.readComponent<camera::CameraComponent>(camera).viewProjectionMatrix;
  const auto &fog = world.getResource<resources::Fog>();

  core::Vec3 lightDir = kDefaultLightDir;
  core::Vec3 lightColor = kDefaultLightColor;
  world.query().read<renderer::DirectionalLightComponent>().each(
      [&](const renderer::DirectionalLightComponent &l) {
        lightDir = l.direction;
        lightColor = l.color;
      });

  FrameData &f = m_data;
  f = FrameData{};
  std::copy_n(core::value_ptr(viewProj), 16, f.viewProj);
  std::copy_n(core::value_ptr(cameraPos), 3, f.cameraPos);
  f.time = static_cast<float>(world.time);
  std::copy_n(core::value_ptr(lightDir), 3, f.lightDir);
  std::copy_n(core::value_ptr(lightColor), 3, f.lightColor);
  std::copy_n(core::value_ptr(kAmbientColor), 3, f.ambientColor);
  f.ambientStrength = kAmbientStrength;
  std::copy_n(core::value_ptr(fog.color), 4, f.fogColor);
  f.fogDensity = fog.density;
  f.fogStart = fog.start;
  f.fogEnd = fog.end;

  api::IRenderDevice &device = world.getRenderDevice();
  if (!m_buffer) {
    m_buffer = device.createBuffer();
  }
  if (m_buffer->size() < sizeof(FrameData)) {
    m_buffer->setData(&m_data, sizeof(FrameData), api::IBuffer::Usage::Dynamic);
  } else {
    m_buffer->setSubData(0, &m_data, sizeof(FrameData));
  }
  device.bindUniformBuffer(kFrameDataBinding, *m_buffer);
}

} // namespace componeng::renderer
