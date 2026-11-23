#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/backend/opengl/gl_texture.hpp"
#include "glad/glad.h"

namespace componeng::renderer::opengl {

/** A framebuffer object with an RGBA8 texture and a depth renderbuffer. */
class GLRenderTarget final : public api::IRenderTarget {
public:
  explicit GLRenderTarget(const api::RenderTargetDesc &desc);
  ~GLRenderTarget() override;

  GLRenderTarget(const GLRenderTarget &) = delete;
  GLRenderTarget &operator=(const GLRenderTarget &) = delete;
  GLRenderTarget(GLRenderTarget &&) = delete;
  GLRenderTarget &operator=(GLRenderTarget &&) = delete;

  api::ITexture &colorTexture() override {
    return m_color;
  }

  GLuint framebuffer() const {
    return m_fbo;
  }

private:
  GLTexture m_color;
  GLuint m_fbo = 0;
  GLuint m_depth = 0;
};

} // namespace componeng::renderer::opengl
