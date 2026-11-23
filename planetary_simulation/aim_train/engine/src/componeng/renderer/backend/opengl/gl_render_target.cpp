#include "componeng/renderer/backend/opengl/gl_render_target.hpp"

#include "componeng/utils/logger.hpp"

namespace componeng::renderer::opengl {

GLRenderTarget::GLRenderTarget(const api::RenderTargetDesc &desc) {
  m_width = desc.width;
  m_height = desc.height;

  api::TextureDesc color;
  color.mipmaps = false;
  m_color.create(m_width, m_height, api::TextureFormat::RGBA8, nullptr, color);

  GLint previous = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);

  glGenFramebuffers(1, &m_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_color.handle(), 0);

  glGenRenderbuffers(1, &m_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, m_width,
                        m_height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, m_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    LOG_ERROR("OpenGL: %ux%u render target is incomplete", m_width, m_height);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
}

GLRenderTarget::~GLRenderTarget() {
  glDeleteFramebuffers(1, &m_fbo);
  glDeleteRenderbuffers(1, &m_depth);
}

} // namespace componeng::renderer::opengl
