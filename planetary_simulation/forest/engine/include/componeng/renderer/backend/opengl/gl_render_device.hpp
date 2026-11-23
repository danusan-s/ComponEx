#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/backend/opengl/gl_buffer.hpp"
#include "componeng/renderer/backend/opengl/gl_mesh.hpp"
#include "componeng/renderer/backend/opengl/gl_render_target.hpp"
#include "componeng/renderer/backend/opengl/gl_shader.hpp"
#include "componeng/renderer/backend/opengl/gl_texture.hpp"
#include "glad/glad.h"

namespace componeng::renderer::opengl {

/**
 * @brief OpenGL implementation of IRenderDevice.
 *
 * Handles GL context loading (glad), global GL state setup, and creates
 * OpenGL-backed resources (shaders, textures, meshes, buffers).
 */
class GLRenderDevice final : public api::IRenderDevice {
public:
  GLRenderDevice() = default;
  ~GLRenderDevice() override = default;

  GLRenderDevice(const GLRenderDevice &) = delete;
  GLRenderDevice &operator=(const GLRenderDevice &) = delete;
  GLRenderDevice(GLRenderDevice &&) = delete;
  GLRenderDevice &operator=(GLRenderDevice &&) = delete;

  void init(void *windowHandle) override;
  void setViewport(int x, int y, int w, int h) override;
  void setScissor(int x, int y, int w, int h) override;
  api::Extent viewportExtent() const override;
  void setActiveTextureUnit(uint32_t unit) override;
  void bindBufferTexture(uint32_t unit, api::IBuffer &buffer,
                         api::BufferTextureFormat format) override;
  void setRenderState(const api::RenderState &state) override;
  void bindUniformBuffer(uint32_t binding, api::IBuffer &buffer) override;
  void clear(float r, float g, float b, float a) override;
  void present(void *windowHandle) override;
  int checkErrors() const override;

  void initImGui(void *windowHandle) override;
  void imguiNewFrame() override;
  void imguiRender() override;
  void shutdownImGui() override;

  void beginGpuTimer() override;
  void endGpuTimer() override;
  double lastGpuTimeMs() const override {
    return m_gpuTimeMs;
  }
  void beginGpuPass(api::PassId pass) override;
  void endGpuPass(api::PassId pass) override;
  double lastGpuPassTimeMs(api::PassId pass) const override {
    return m_gpuPasses[static_cast<std::size_t>(pass)].ms;
  }

  std::unique_ptr<api::IShader> createShader() override;
  std::unique_ptr<api::ITexture> createTexture() override;
  std::unique_ptr<api::IMesh> createMesh() override;
  std::unique_ptr<api::IBuffer> createBuffer() override;
  std::unique_ptr<api::IRenderTarget>
  createRenderTarget(const api::RenderTargetDesc &desc) override;
  void setRenderTarget(api::IRenderTarget *target) override;

  void setupInstanceAttributes(api::IBuffer &instanceBuffer,
                               const api::VertexLayout &layout) override;
  void unbindInstanceAttributes(const api::VertexLayout &layout) override;

  void drawIndexedInstanced(size_t indexCount, uint32_t instanceCount) override;
  void drawFullscreenTriangle() override;

private:
  /** Issues GL calls for the parts of `state` that differ from m_state (all of
   *  it when `force`), then records it as current. */
  void applyRenderState(const api::RenderState &state, bool force);

  api::RenderState m_state;

  // What setViewport / setScissor last gave for the window. They are not sent
  // to GL while a render target is bound, which uses its own full size.
  int m_viewport[4] = {0, 0, 0, 0};
  int m_scissor[4] = {0, 0, 0, 0};
  const GLRenderTarget *m_target = nullptr;

  // Core-profile GL refuses to draw without a VAO bound, even one with no
  // attributes. Created once in init() and kept for the life of the context.
  unsigned int m_fullscreenVao = 0;

  // GPU timer: a small ring of GL_TIME_ELAPSED queries. A query's result is
  // only read once the GPU reports it available, so reading never stalls the
  // pipeline; with several in flight a result normally lands 2-3 frames after
  // its span. The queries are not deleted explicitly: they go with the GL
  // context, and the device can outlive it during shutdown.
  static constexpr int kGpuQueryCount = 4;
  unsigned int m_gpuQueries[kGpuQueryCount] = {};
  bool m_gpuPending[kGpuQueryCount] = {};
  int m_gpuNext = 0;          // slot the next span will use
  bool m_gpuSpanOpen = false; // begin() issued a query that end() must close
  bool m_gpuCreated = false;
  double m_gpuTimeMs = -1.0;

  // Per-pass timers: each pass has its own ring of timestamp pairs. They are
  // timestamps (glQueryCounter), not GL_TIME_ELAPSED, because only one
  // GL_TIME_ELAPSED query can be active at a time and the frame timer above
  // holds it. Read in collectGpuPasses(), from endGpuTimer().
  struct GpuPassTimer {
    unsigned int queries[kGpuQueryCount][2] = {}; // begin, end
    bool pending[kGpuQueryCount] = {};
    int next = 0;
    bool open = false;
    bool created = false;
    double ms = -1.0;
  };
  GpuPassTimer m_gpuPasses[api::kPassCount];

  void collectGpuPasses();
};

} // namespace componeng::renderer::opengl
