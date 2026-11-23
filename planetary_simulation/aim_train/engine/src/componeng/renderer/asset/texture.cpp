#include "componeng/renderer/asset/texture.hpp"

#include "componeng/utils/logger.hpp"

namespace componeng::renderer {

void Texture2D::generate(api::IRenderDevice &device, uint32_t width,
                         uint32_t height, unsigned char *data, bool alpha,
                         const api::TextureDesc &desc) {
  m_impl = device.createTexture();
  m_impl->generate(width, height, data, alpha, desc);
}

void Texture2D::create(api::IRenderDevice &device, uint32_t width,
                       uint32_t height, api::TextureFormat format,
                       const void *pixels, const api::TextureDesc &desc) {
  m_impl = device.createTexture();
  m_impl->create(width, height, format, pixels, desc);
}

bool Texture2D::update(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                       const void *pixels) {
  if (!m_impl || width == 0 || height == 0 || x > m_impl->width() ||
      y > m_impl->height() || width > m_impl->width() - x ||
      height > m_impl->height() - y) {
    LOG_ERROR("Texture update region (%u,%u %ux%u) is outside the %ux%u "
              "texture",
              x, y, width, height, m_impl ? m_impl->width() : 0,
              m_impl ? m_impl->height() : 0);
    return false;
  }
  m_impl->update(x, y, width, height, pixels);
  return true;
}

void Texture2D::bind() const {
  m_impl->bind();
}

uint32_t Texture2D::width() const {
  return m_impl ? m_impl->width() : 0;
}

uint32_t Texture2D::height() const {
  return m_impl ? m_impl->height() : 0;
}

} // namespace componeng::renderer
