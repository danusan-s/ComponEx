#include "componeng/renderer/backend/opengl/gl_buffer.hpp"

#include "componeng/utils/logger.hpp"

namespace componeng::renderer::opengl {

GLBuffer::GLBuffer() {
  glGenBuffers(1, &m_id);
}

GLBuffer::~GLBuffer() {
  release();
}

void GLBuffer::setData(const void *data, size_t sizeBytes,
                       IBuffer::Usage usage) {
  m_size = sizeBytes;
  glBindBuffer(GL_ARRAY_BUFFER, m_id);
  GLenum glUsage =
      (usage == IBuffer::Usage::Static) ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;
  glBufferData(GL_ARRAY_BUFFER, sizeBytes, data, glUsage);
}

void GLBuffer::setSubData(size_t offset, const void *data, size_t sizeBytes) {
  glBindBuffer(GL_ARRAY_BUFFER, m_id);
  glBufferSubData(GL_ARRAY_BUFFER, offset, sizeBytes, data);
}

GLuint GLBuffer::bufferTexture() {
  if (m_bufferTexture == 0) {
    glGenTextures(1, &m_bufferTexture);
  }
  return m_bufferTexture;
}

void GLBuffer::release() {
  if (m_bufferTexture != 0) {
    glDeleteTextures(1, &m_bufferTexture);
    m_bufferTexture = 0;
  }
  if (m_id != 0) {
    glDeleteBuffers(1, &m_id);
    m_id = 0;
  }
}

} // namespace componeng::renderer::opengl
