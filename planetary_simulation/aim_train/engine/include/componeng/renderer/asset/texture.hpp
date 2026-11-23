#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"

#include <memory>

namespace componeng::renderer {

/**
 * @brief 2D texture wrapper with generation, configuration, and binding.
 *
 * Backed by the ITexture interface.
 */
class Texture2D {
public:
  Texture2D() = default;

  /** Creates the GPU texture through `device` from 8-bit pixel data. */
  void generate(api::IRenderDevice &device, uint32_t width, uint32_t height,
                unsigned char *data, bool alpha = false,
                const api::TextureDesc &desc = {});

  /** Creates the GPU texture through `device` from pixels in any
   *  api::TextureFormat. See ITexture::create for the data layout. */
  void create(api::IRenderDevice &device, uint32_t width, uint32_t height,
              api::TextureFormat format, const void *pixels,
              const api::TextureDesc &desc = {});

  /** Overwrites a region of the texture. Returns false, changing nothing and
   *  logging an error, if the region does not lie inside the texture. */
  bool update(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
              const void *pixels);

  void bind() const;

  uint32_t width() const;
  uint32_t height() const;

private:
  std::unique_ptr<api::ITexture> m_impl;
};

} // namespace componeng::renderer
