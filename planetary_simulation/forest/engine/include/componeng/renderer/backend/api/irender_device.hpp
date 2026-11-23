#pragma once

#include "componeng/renderer/backend/api/pass_id.hpp"

#include "componeng/core/types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace componeng::renderer::api {

/**
 * @brief Describes the layout of a single vertex attribute.
 */
struct VertexAttribute {
  core::Name name;
  uint32_t offset = 0;
  uint32_t componentCount = 0;
  bool normalized = false;
  // >= 0 if this slot holds one row of a mat4-typed value (see
  // IShader::reflectInstanceLayout); componentCount is then that row's width
  // and `name` is the mat4 value's own name, shared across its 4 rows.
  int matrixRow = -1;
};

/**
 * @brief Describes the full vertex layout: attributes list + stride in bytes.
 */
struct VertexLayout {
  std::vector<VertexAttribute> attributes;
  uint32_t stride = 0;
};

/**
 * @brief GL attribute locations 0-2 are always reserved for mesh
 *        position/normal/uv (see defaultMeshLayout); per-instance material
 *        attributes are bound starting from this location.
 */
constexpr uint32_t kFirstInstanceAttribLocation = 3;

/**
 * @brief Texture units and uniform-buffer binding points the engine hands out.
 *
 * Unit 0 is the material's albedo and units 1-4 the PBR maps of a PbrMaterial;
 * extra textures a material binds with IMaterial::setTexture start after
 * those. Uniform-buffer binding 0 is the per-frame FrameData block; a
 * material's own blocks start after it.
 */
constexpr uint32_t kFirstMaterialTextureUnit = 5;
constexpr uint32_t kTextureUnitLimit = 16;
constexpr uint32_t kFirstMaterialUniformBinding = 1;
constexpr uint32_t kUniformBindingLimit = 16;

/**
 * @brief Default mesh vertex layout: position (3), normal (3), uv (2).
 *        Stride = 32 bytes.
 */
inline VertexLayout defaultMeshLayout() {
  VertexLayout layout;
  layout.attributes = {
      {"position", 0, 3, false},
      {"normal", 12, 3, false},
      {"uv", 24, 2, false},
  };
  layout.stride = 32;
  return layout;
}

/**
 * @brief How a buffer is read when bound as a texture buffer (a
 * `samplerBuffer` in GLSL, read with texelFetch): one float, two or four per
 * texel.
 */
enum class BufferTextureFormat { R32F, RG32F, RGBA32F };

/**
 * @brief API-agnostic buffer interface.
 *
 * Represents a GPU buffer (vertex, index, uniform, or instance data).
 */
class IBuffer {
public:
  enum class Usage { Static, Dynamic };

  virtual ~IBuffer() = default;

  /** Upload data to the buffer. */
  virtual void setData(const void *data, size_t sizeBytes,
                       Usage usage = Usage::Dynamic) = 0;

  /** Upload a sub-region of the buffer (for dynamic updates). */
  virtual void setSubData(size_t offset, const void *data,
                          size_t sizeBytes) = 0;

  /** Current allocated size in bytes (last size passed to setData), 0 if
   *  setData has never been called. Lets callers decide whether a new
   *  upload fits via setSubData or needs setData to grow the buffer. */
  virtual size_t size() const = 0;

  /** Release GPU resources. */
  virtual void release() = 0;
};

/**
 * @brief API-agnostic shader interface.
 *
 * Wraps a compiled shader program with typed uniform setters.
 * Backends may load GLSL source (OpenGL) or SPIRV binaries (Vulkan).
 */
class IShader {
public:
  virtual ~IShader() = default;

  /** Load shader from GLSL source strings (OpenGL path). */
  virtual void loadGLSL(const char *vertexSource, const char *fragmentSource,
                        const char *geometrySource = nullptr) = 0;

  /** Load shader from SPIRV binary files (Vulkan path). */
  virtual void loadSPIRV(const char *vertexPath, const char *fragmentPath,
                         const char *geometryPath = nullptr) = 0;

  /** Activate this shader for subsequent uniform/draw calls. */
  virtual void use() const = 0;

  /** Release GPU resources. */
  virtual void release() = 0;

  /**
   * Per-shader GPU timing. Work submitted between beginGpuTimer() and
   * endGpuTimer() is attributed to this shader; a shader may be timed in
   * several spans per frame and they are summed. collectGpuTimer() marks the
   * end of a frame (call it once per frame for every shader, after the frame's
   * commands are submitted) and picks up results the GPU has finished.
   * Results arrive a few frames late rather than stalling, so lastGpuTimeMs()
   * reports an older frame. Spans must not nest within one shader.
   *
   * Optional: the defaults do nothing and report "unavailable", so backends
   * without timer queries (and test fakes) need not implement them. The
   * methods are const, like use(), so they work on shaders held by const
   * reference.
   */
  virtual void beginGpuTimer() const {
  }
  virtual void endGpuTimer() const {
  }
  virtual void collectGpuTimer() const {
  }

  /** Milliseconds of GPU work attributed to this shader in the most recent
   *  completed frame (0 if it was not used), or a negative value if none has
   *  completed yet (or timing is unsupported). */
  virtual double lastGpuTimeMs() const {
    return -1.0;
  }

  // Uniform setters
  virtual void setFloat(const char *name, float value) const = 0;
  virtual void setInteger(const char *name, int value) const = 0;
  virtual void setVector2f(const char *name, float x, float y) const = 0;
  virtual void setVector3f(const char *name, float x, float y,
                           float z) const = 0;
  virtual void setVector4f(const char *name, float x, float y, float z,
                           float w) const = 0;
  virtual void setMatrix4(const char *name, const float *matrix) const = 0;

  /**
   * Reflect the linked program's active per-instance vertex attributes
   * (locations >= kFirstInstanceAttribLocation) into a VertexLayout. A
   * mat4-typed attribute is reported by GL as a single active attribute
   * spanning 4 locations; it is auto-expanded into 4 row entries here so
   * callers only ever address it by its one shader-declared name.
   */
  virtual VertexLayout reflectInstanceLayout() const = 0;

  /**
   * Attach the shader's uniform block `blockName` (if it declares one) to
   * uniform-buffer binding point `binding`. Returns false when the shader has
   * no such block. If `expectedSizeBytes` is non-zero and the block's size in
   * this shader differs, an error is logged: it means the shader's declaration
   * and the CPU-side struct have drifted apart.
   */
  virtual bool bindUniformBlock(const char *blockName, uint32_t binding,
                                size_t expectedSizeBytes = 0) = 0;

  /** Reflect the linked program's active plain uniform names (for validating
   *  setUniform() calls against the shader that will actually consume them).
   *  Members of uniform blocks are not included: they cannot be set one by
   *  one. */
  virtual std::vector<core::Name> reflectActiveUniformNames() const = 0;
};

/** What a sampler does outside the [0, 1] texture-coordinate range. */
enum class TextureWrap { ClampToEdge, Repeat, MirroredRepeat };

/** Sampling filter. */
enum class TextureFilter { Nearest, Linear };

/**
 * @brief Storage format of a texture created from memory.
 *
 * 8-bit formats take unsigned bytes (normalised to [0, 1] when sampled); the
 * 16F and 32F formats take 32-bit floats on the CPU side, stored on the GPU as
 * half or full floats and sampled unnormalised. Float textures are the way to
 * hand a shader real data (heights, densities) rather than colour.
 */
enum class TextureFormat {
  R8,
  RG8,
  RGB8,
  RGBA8,
  R16F,
  RG16F,
  RGBA16F,
  R32F,
  RG32F,
  RGBA32F
};

constexpr uint32_t textureChannels(TextureFormat format) {
  switch (format) {
    case TextureFormat::R8:
    case TextureFormat::R16F:
    case TextureFormat::R32F:
      return 1;
    case TextureFormat::RG8:
    case TextureFormat::RG16F:
    case TextureFormat::RG32F:
      return 2;
    case TextureFormat::RGB8:
      return 3;
    case TextureFormat::RGBA8:
    case TextureFormat::RGBA16F:
    case TextureFormat::RGBA32F:
      return 4;
  }
  return 0;
}

constexpr bool textureIsFloat(TextureFormat format) {
  return format != TextureFormat::R8 && format != TextureFormat::RG8 &&
         format != TextureFormat::RGB8 && format != TextureFormat::RGBA8;
}

/** Bytes per pixel of the CPU-side data a TextureFormat expects. */
constexpr size_t textureCpuPixelBytes(TextureFormat format) {
  return textureChannels(format) * (textureIsFloat(format) ? 4 : 1);
}

/**
 * @brief How a texture is stored and sampled.
 *
 * The defaults reproduce the engine's original fixed behaviour (clamped,
 * trilinear-filtered, linear colour), so a texture loaded without a
 * TextureDesc looks exactly as it always did.
 */
struct TextureDesc {
  TextureWrap wrap = TextureWrap::ClampToEdge;
  TextureFilter minFilter = TextureFilter::Linear;
  TextureFilter magFilter = TextureFilter::Linear;
  /** Build a mip chain and filter between levels when minifying. */
  bool mipmaps = true;
  /** Anisotropic filtering, 1 = off. Clamped to what the device supports;
   *  silently ignored where it is unsupported. */
  float anisotropy = 1.0f;
  /**
   * Treat the pixel data as sRGB-encoded colour, so sampling returns linear
   * values. Use it for colour images (albedo) read by a shader that does its
   * lighting in linear space and encodes its own output, as pbr.frag does.
   * Leave it off for data (normals, roughness, masks) and for shaders that
   * use the sampled value directly as the final colour, which would come out
   * too dark.
   */
  bool srgb = false;
};

/**
 * @brief API-agnostic 2D texture interface.
 */
class ITexture {
public:
  virtual ~ITexture() = default;

  /** Create the GPU texture from pixel data (RGBA or RGB, 8-bit). */
  virtual void generate(uint32_t width, uint32_t height,
                        const unsigned char *data, bool alpha,
                        const TextureDesc &desc) = 0;

  /**
   * Create the GPU texture in any TextureFormat. `pixels` holds
   * width * height * textureCpuPixelBytes(format) bytes, rows tightly packed,
   * top row first; null allocates the storage with undefined contents.
   * `desc.srgb` applies to RGB8 and RGBA8 only.
   */
  virtual void create(uint32_t width, uint32_t height, TextureFormat format,
                      const void *pixels, const TextureDesc &desc) = 0;

  /** Overwrite a width x height region at (x, y) of a texture made with
   *  create(), with pixels in that texture's format. Mip levels are rebuilt
   *  when the texture has them. The region must lie inside the texture. */
  virtual void update(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                      const void *pixels) = 0;

  /** Bind this texture to the active texture unit / descriptor set. */
  virtual void bind() const = 0;

  /** Release GPU resources. */
  virtual void release() = 0;

  uint32_t width() const {
    return m_width;
  }
  uint32_t height() const {
    return m_height;
  }

protected:
  uint32_t m_width = 0;
  uint32_t m_height = 0;
};

/** A size in pixels. */
struct Extent {
  uint32_t width = 0;
  uint32_t height = 0;

  bool operator==(const Extent &o) const {
    return width == o.width && height == o.height;
  }
  bool operator!=(const Extent &o) const {
    return !(*this == o);
  }
};

struct RenderTargetDesc {
  uint32_t width = 0;
  uint32_t height = 0;
};

/**
 * @brief An off-screen image the device can draw into instead of the window:
 * an RGBA8 colour attachment plus a depth buffer.
 *
 * Made with IRenderDevice::createRenderTarget, drawn into after
 * setRenderTarget(), and read back by sampling its colour texture.
 */
class IRenderTarget {
public:
  virtual ~IRenderTarget() = default;

  /**
   * The colour attachment as a texture (linear filtered, clamped, no mips).
   * Sample it with ITexture::bind() once the target is no longer the one being
   * drawn into. Sampled like any other texture: v = 0 is the bottom of the
   * picture in every backend. Fetch it again each frame: a backend may rotate
   * the images behind it.
   */
  virtual ITexture &colorTexture() = 0;

  uint32_t width() const {
    return m_width;
  }
  uint32_t height() const {
    return m_height;
  }

protected:
  uint32_t m_width = 0;
  uint32_t m_height = 0;
};

/**
 * @brief API-agnostic mesh interface.
 *
 * Holds vertex and index data along with a vertex layout descriptor.
 */
class IMesh {
public:
  virtual ~IMesh() = default;

  /** Upload vertex and index data to the GPU using the given layout. */
  virtual void upload(const float *vertices, size_t vertexCount,
                      const uint32_t *indices, size_t indexCount,
                      const VertexLayout &layout) = 0;

  /** Bind the mesh for rendering (binds VAO / vertex buffers). */
  virtual void bind() const = 0;

  /** Release GPU resources. */
  virtual void release() = 0;

  /** Number of indices in the index buffer. */
  virtual size_t indexCount() const = 0;
};

/** Which triangle faces are discarded (front faces are counter-clockwise). */
enum class CullMode { None, Back, Front };

/** Depth comparison used by the depth test. */
enum class DepthFunc { Less, LessEqual, Equal };

/** How a fragment's colour is combined with what is already in the target. */
enum class BlendMode {
  None,     // overwrite
  Alpha,    // src * a + dst * (1 - a)
  Additive, // src * a + dst
};

/**
 * @brief Fixed-function pipeline state for a draw.
 *
 * The defaults are the engine's baseline (back-face culling, depth test and
 * write with GL_LESS, alpha blending). A material carries one of these and
 * the RenderSystem applies it before drawing; the device only touches the
 * API for the parts that actually differ from what is currently set.
 */
struct RenderState {
  CullMode cull = CullMode::Back;
  bool depthTest = true;
  bool depthWrite = true;
  DepthFunc depthFunc = DepthFunc::Less;
  BlendMode blend = BlendMode::Alpha;
  // False masks out every colour channel: the draw only touches depth.
  bool colorWrite = true;

  bool operator==(const RenderState &o) const {
    return cull == o.cull && depthTest == o.depthTest &&
           depthWrite == o.depthWrite && depthFunc == o.depthFunc &&
           blend == o.blend && colorWrite == o.colorWrite;
  }
  bool operator!=(const RenderState &o) const {
    return !(*this == o);
  }
};

/**
 * @brief API-agnostic render device interface.
 *
 * Manages the rendering API lifecycle: initialization, state, clear,
 * presentation, and resource creation.
 */
class IRenderDevice {
public:
  virtual ~IRenderDevice() = default;

  /** Initialize the rendering API. windowHandle is a GLFWwindow*. */
  virtual void init(void *windowHandle) = 0;

  /** Set the viewport rectangle. */
  virtual void setViewport(int x, int y, int w, int h) = 0;

  /** Set the scissor rectangle. */
  virtual void setScissor(int x, int y, int w, int h) = 0;

  /** The window area drawn into, as last given to setViewport (the whole
   *  window before the first call). A render target the same size shows 1:1. */
  virtual Extent viewportExtent() const = 0;

  /** Clear color and depth buffers of whatever is being drawn into. */
  virtual void clear(float r, float g, float b, float a) = 0;

  /** Present the rendered frame. */
  virtual void present(void *windowHandle) = 0;

  /** Release objects that must go before the window (swapchain, surface). */
  virtual void shutdown() {
  }

  /** Dear ImGui hooks; the defaults do nothing. imguiRender draws the UI on
   *  top of the finished frame, just before present(). */
  virtual void initImGui(void * /*windowHandle*/) {
  }
  virtual void imguiNewFrame() {
  }
  virtual void imguiRender() {
  }
  virtual void shutdownImGui() {
  }

  /** Check for API errors and log them. Returns error count. */
  virtual int checkErrors() const = 0;

  /**
   * GPU timing. Everything submitted between beginGpuTimer() and
   * endGpuTimer() is timed on the GPU. The result is not available until the
   * GPU has finished that work, so lastGpuTimeMs() reports a frame that is a
   * few frames old rather than stalling to wait for the current one. One
   * begin/end pair per frame; they must not nest.
   *
   * Optional: the defaults do nothing and report "unavailable", so devices
   * without timer queries (and test fakes) need not implement them.
   */
  virtual void beginGpuTimer() {
  }
  virtual void endGpuTimer() {
  }

  /** Milliseconds of GPU work in the most recent completed timed span, or a
   *  negative value if none has completed (or timing is unsupported). */
  virtual double lastGpuTimeMs() const {
    return -1.0;
  }

  /**
   * Per-pass GPU timing. Work submitted between beginGpuPass(p) and
   * endGpuPass(p) is timed as pass p, once per frame; passes may sit inside the
   * beginGpuTimer()/endGpuTimer() span and inside each other's gaps, but a pass
   * does not nest with itself. Like the frame timer, results arrive a few
   * frames late, and are collected when endGpuTimer() runs.
   * lastGpuPassTimeMs() is the newest finished frame's time for that pass, or
   * negative if none has finished (or timing is unsupported). Optional, like
   * the frame timer.
   */
  virtual void beginGpuPass(PassId) {
  }
  virtual void endGpuPass(PassId) {
  }
  virtual double lastGpuPassTimeMs(PassId) const {
    return -1.0;
  }

  // Factory methods
  virtual std::unique_ptr<IShader> createShader() = 0;
  virtual std::unique_ptr<ITexture> createTexture() = 0;
  virtual std::unique_ptr<IMesh> createMesh() = 0;
  virtual std::unique_ptr<IBuffer> createBuffer() = 0;

  /** A colour + depth target of the given size. Its contents are undefined at
   *  the start of every frame: clear it before drawing. */
  virtual std::unique_ptr<IRenderTarget>
  createRenderTarget(const RenderTargetDesc &desc) = 0;

  /**
   * Draw into `target` from now on; nullptr goes back to the window.
   *
   * While a target is bound, viewport and scissor cover all of it, whatever
   * setViewport and setScissor were last given; those apply again on return.
   * A target must not be sampled while it is bound. Anything the frame needs
   * on the window (the debug UI, present) expects the window to be bound.
   */
  virtual void setRenderTarget(IRenderTarget *target) = 0;

  /** Apply pipeline state. Cheap to call every draw: only the differences
   *  from the current state reach the graphics API. */
  virtual void setRenderState(const RenderState &state) = 0;

  /** Bind `buffer` to uniform-buffer binding point `binding`; shaders that
   *  declare a matching uniform block read from it. */
  virtual void bindUniformBuffer(uint32_t binding, IBuffer &buffer) = 0;

  /** Set the active texture unit for subsequent texture binds. */
  virtual void setActiveTextureUnit(uint32_t unit) = 0;

  /** Bind `buffer` to texture unit `unit` as a texture buffer, so a shader's
   *  `samplerBuffer` on that unit can texelFetch its contents. */
  virtual void bindBufferTexture(uint32_t unit, IBuffer &buffer,
                                 BufferTextureFormat format) = 0;

  /**
   * Configure instance vertex attributes on the currently bound mesh.
   *  The instanceBuffer holds per-instance model matrices (InstanceData).
   *  This is an OpenGL-specific concept; Vulkan implementations should be
   *  no-ops since instance attributes are configured at pipeline creation.
   */
  virtual void setupInstanceAttributes(IBuffer &instanceBuffer,
                                       const VertexLayout &layout) = 0;

  /** Unbind instance vertex attributes. No-op for Vulkan. */
  virtual void unbindInstanceAttributes(const VertexLayout &layout) = 0;

  /** Draw one triangle that covers the whole viewport, with no vertex or
   *  index data: the vertex shader derives positions from gl_VertexID. Used
   *  for full-screen effects such as the sky. */
  virtual void drawFullscreenTriangle() = 0;

  /** Issue a draw call: indexed instanced rendering. */
  virtual void drawIndexedInstanced(size_t indexCount,
                                    uint32_t instanceCount) = 0;
};

} // namespace componeng::renderer::api
