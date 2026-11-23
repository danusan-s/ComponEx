#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "glad/glad.h"

namespace componeng::renderer::opengl {

class GLTexture final : public api::ITexture {
public:
  GLTexture() = default;
  ~GLTexture() override;

  // Owns a GL handle freed in the dtor; copying it would double-free
  GLTexture(const GLTexture &) = delete;
  GLTexture &operator=(const GLTexture &) = delete;
  GLTexture(GLTexture &&) = delete;
  GLTexture &operator=(GLTexture &&) = delete;

  void generate(uint32_t width, uint32_t height, const unsigned char *data,
                bool alpha, const api::TextureDesc &desc) override;
  void create(uint32_t width, uint32_t height, api::TextureFormat format,
              const void *pixels, const api::TextureDesc &desc) override;
  void update(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
              const void *pixels) override;
  void bind() const override;
  void release() override;

  GLuint handle() const {
    return m_id;
  }

private:
  GLuint m_id = 0;
  api::TextureFormat m_format = api::TextureFormat::RGBA8;
  bool m_mipmaps = false;
};

} // namespace componeng::renderer::opengl
