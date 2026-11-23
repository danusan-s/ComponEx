#include "componeng/renderer/backend/opengl/gl_texture.hpp"

#include <algorithm>
#include <cstring>

namespace componeng::renderer::opengl {

namespace {

// GL_EXT_texture_filter_anisotropic (and the identical ARB variant promoted
// into core in 4.6). Not part of the generated 3.3 loader, so the enums are
// spelled out and support is probed once.
constexpr GLenum kTextureMaxAnisotropy = 0x84FE;
constexpr GLenum kMaxTextureMaxAnisotropy = 0x84FF;

// Largest supported anisotropy, or 1 when the extension is absent.
float maxAnisotropy() {
  static const float value = [] {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
      const char *name =
          reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
      if (name &&
          (std::strcmp(name, "GL_EXT_texture_filter_anisotropic") == 0 ||
           std::strcmp(name, "GL_ARB_texture_filter_anisotropic") == 0)) {
        GLfloat maxValue = 1.0f;
        glGetFloatv(kMaxTextureMaxAnisotropy, &maxValue);
        return std::max(1.0f, maxValue);
      }
    }
    return 1.0f;
  }();
  return value;
}

GLint toGLWrap(api::TextureWrap wrap) {
  switch (wrap) {
    case api::TextureWrap::Repeat:
      return GL_REPEAT;
    case api::TextureWrap::MirroredRepeat:
      return GL_MIRRORED_REPEAT;
    case api::TextureWrap::ClampToEdge:
      break;
  }
  return GL_CLAMP_TO_EDGE;
}

GLint toGLMinFilter(api::TextureFilter filter, bool mipmaps) {
  if (filter == api::TextureFilter::Nearest) {
    return mipmaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
  }
  return mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
}

GLint toGLMagFilter(api::TextureFilter filter) {
  return filter == api::TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR;
}

// How a TextureFormat is stored (internal format), and how the CPU data is
// laid out (pixel format and component type).
struct GLFormat {
  GLint internal;
  GLenum pixel;
  GLenum type;
};

GLFormat toGLFormat(api::TextureFormat format, bool srgb) {
  switch (format) {
    case api::TextureFormat::R8:
      return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
    case api::TextureFormat::RG8:
      return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
    case api::TextureFormat::RGB8:
      return {srgb ? GL_SRGB8 : GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
    case api::TextureFormat::RGBA8:
      return {srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    case api::TextureFormat::R16F:
      return {GL_R16F, GL_RED, GL_FLOAT};
    case api::TextureFormat::RG16F:
      return {GL_RG16F, GL_RG, GL_FLOAT};
    case api::TextureFormat::RGBA16F:
      return {GL_RGBA16F, GL_RGBA, GL_FLOAT};
    case api::TextureFormat::R32F:
      return {GL_R32F, GL_RED, GL_FLOAT};
    case api::TextureFormat::RG32F:
      return {GL_RG32F, GL_RG, GL_FLOAT};
    case api::TextureFormat::RGBA32F:
      return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
  }
  return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
}

} // namespace

GLTexture::~GLTexture() {
  release();
}

void GLTexture::generate(uint32_t width, uint32_t height,
                         const unsigned char *data, bool alpha,
                         const api::TextureDesc &desc) {
  create(width, height,
         alpha ? api::TextureFormat::RGBA8 : api::TextureFormat::RGB8, data,
         desc);
}

void GLTexture::create(uint32_t width, uint32_t height,
                       api::TextureFormat format, const void *pixels,
                       const api::TextureDesc &desc) {
  m_width = width;
  m_height = height;
  m_format = format;
  m_mipmaps = desc.mipmaps;

  if (m_id == 0) {
    glGenTextures(1, &m_id);
  }
  glBindTexture(GL_TEXTURE_2D, m_id);

  const GLFormat f = toGLFormat(format, desc.srgb);

  // Rows of tightly packed RGB are 3 * width bytes, which is not a multiple of
  // GL's default 4-byte row alignment for most widths (and the same goes for
  // one- and two-channel bytes); without this such images upload skewed.
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, f.internal, width, height, 0, f.pixel, f.type,
               pixels);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

  if (desc.mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, toGLWrap(desc.wrap));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, toGLWrap(desc.wrap));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  toGLMinFilter(desc.minFilter, desc.mipmaps));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  toGLMagFilter(desc.magFilter));

  if (desc.anisotropy > 1.0f && maxAnisotropy() > 1.0f) {
    glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropy,
                    std::min(desc.anisotropy, maxAnisotropy()));
  }

  glBindTexture(GL_TEXTURE_2D, 0);
}

void GLTexture::update(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                       const void *pixels) {
  if (m_id == 0) {
    return;
  }
  // The pixel format and type do not depend on sRGB; the texture's own
  // internal format already decides how the values are interpreted.
  const GLFormat f = toGLFormat(m_format, false);

  glBindTexture(GL_TEXTURE_2D, m_id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, f.pixel, f.type,
                  pixels);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  if (m_mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }
  glBindTexture(GL_TEXTURE_2D, 0);
}

void GLTexture::bind() const {
  glBindTexture(GL_TEXTURE_2D, m_id);
}

void GLTexture::release() {
  if (m_id != 0) {
    glDeleteTextures(1, &m_id);
    m_id = 0;
  }
}

} // namespace componeng::renderer::opengl
