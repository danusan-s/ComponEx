#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "glad/glad.h"

namespace componeng::renderer::opengl {

class GLBuffer final : public api::IBuffer {
public:
  GLBuffer();
  ~GLBuffer() override;

  // Owns a GL handle freed in the dtor; copying it would double-free
  GLBuffer(const GLBuffer &) = delete;
  GLBuffer &operator=(const GLBuffer &) = delete;
  GLBuffer(GLBuffer &&) = delete;
  GLBuffer &operator=(GLBuffer &&) = delete;

  void
  setData(const void *data, size_t sizeBytes,
          api::IBuffer::Usage usage = api::IBuffer::Usage::Dynamic) override;
  void setSubData(size_t offset, const void *data, size_t sizeBytes) override;
  size_t size() const override {
    return m_size;
  }
  void release() override;

  GLuint handle() const {
    return m_id;
  }

  /** The GL texture object through which this buffer is read as a texture
   *  buffer; created on first use. */
  GLuint bufferTexture();

private:
  GLuint m_id = 0;
  GLuint m_bufferTexture = 0;
  size_t m_size = 0;
};

} // namespace componeng::renderer::opengl
