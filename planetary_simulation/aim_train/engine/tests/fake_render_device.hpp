#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// A render device that draws nothing and records what it was asked to do, so
// the engine's batching and render systems can be tested without a GL context.
// It also demonstrates that asset loading no longer needs OpenGL: every
// GPU-side object comes from the device's factory methods.
namespace fake {

using namespace componeng::renderer;

struct Draw {
  size_t indexCount = 0;
  uint32_t instanceCount = 0;
  api::RenderState state;            // pipeline state at the moment of the draw
  int shaderId = 0;                  // shader in use
  int albedoTextureId = 0;           // texture bound to unit 0
  std::vector<float> instanceFloats; // instance buffer contents at draw time
  // What was bound on each texture unit at the time of the draw (bindings
  // persist between draws, as in GL, so units a material does not use may
  // still show an earlier material's texture).
  std::map<uint32_t, int> textureUnits;       // unit -> texture id
  std::map<uint32_t, int> bufferTextureUnits; // unit -> buffer id
  int targetId = 0; // render target drawn into (0: window)
};

// A render target made through createRenderTarget, in creation order. `id`
// is what Draw::targetId reports; `colorTextureId` the id its colour texture
// binds as.
struct CreatedTarget {
  int id = 0;
  uint32_t width = 0, height = 0;
  int colorTextureId = 0;
};

struct UniformBufferBind {
  uint32_t binding = 0;
  std::vector<unsigned char> bytes; // buffer contents at bind time
};

// A texture made through ITexture::create (as opposed to generate).
struct CreatedTexture {
  int id = 0;
  uint32_t width = 0, height = 0;
  api::TextureFormat format = api::TextureFormat::RGBA8;
  api::TextureDesc desc;
  bool hadPixels = false;
  std::vector<unsigned char> pixels;
};

struct TextureUpdate {
  int id = 0;
  uint32_t x = 0, y = 0, width = 0, height = 0;
  std::vector<unsigned char> pixels;
};

struct BufferTextureBind {
  uint32_t unit = 0;
  int bufferId = 0;
  api::BufferTextureFormat format = api::BufferTextureFormat::R32F;
  std::vector<unsigned char> bytes;
};

// Everything the fake device observed, in one place so shaders, textures and
// meshes (which outlive any single call) can report into it.
struct Recorder {
  int shadersCreated = 0;
  int texturesCreated = 0;
  int meshesCreated = 0;
  int buffersCreated = 0;

  int clears = 0;
  // Render targets: the one currently bound (0: the window), every
  // setRenderTarget call in order, and which one each clear() hit.
  int currentTarget = 0;
  std::vector<int> targetBinds;
  std::vector<int> clearTargetIds;
  std::vector<CreatedTarget> createdTargets;
  // What viewportExtent() reports; tests change it to resize the window.
  api::Extent viewport{1280, 720};
  std::vector<api::RenderState> stateRequests; // every setRenderState call
  std::vector<int> shaderUses;                 // shader id per use()
  std::vector<int> unit0TextureBinds;          // texture id per unit-0 bind
  std::vector<UniformBufferBind> uniformBufferBinds;
  std::vector<CreatedTexture> createdTextures;
  std::vector<TextureUpdate> textureUpdates;
  std::vector<BufferTextureBind> bufferTextureBinds;
  // (block name, binding point) for every IShader::bindUniformBlock call.
  std::vector<std::pair<std::string, uint32_t>> uniformBlockBinds;
  // Uniform blocks (besides FrameData) the fake shaders claim to declare.
  std::set<std::string> declaredBlocks;
  std::map<uint32_t, int> boundBufferTextureByUnit;
  std::vector<Draw> draws;
  std::vector<Draw> fullscreenDraws;     // drawFullscreenTriangle calls
  std::vector<std::string> drawSequence; // "fullscreen" / "indexed", in order
  // beginGpuPass / endGpuPass calls as "begin <name>" / "end <name>", with each
  // draw ("indexed" / "fullscreen") between them, in order.
  std::vector<std::string> gpuPassEvents;

  // The fragment source each shader id was compiled from (IShader::loadGLSL).
  std::map<int, std::string> fragmentSources;

  // Names passed to loose uniform setters, per shader id.
  std::map<int, std::vector<std::string>> looseUniforms;
  // The latest value given to each float uniform, per shader id.
  std::map<int, std::map<std::string, float>> floatUniforms;

  // Configuration the tests can change before creating assets.
  api::VertexLayout instanceLayout;
  std::map<int, bool> shaderDeclaresFrameBlock; // by shader id (creation order)
  bool defaultShaderDeclaresFrameBlock = false;

  // Current state, updated as calls arrive.
  api::RenderState currentState;
  int currentShader = 0;
  uint32_t activeUnit = 0;
  std::map<uint32_t, int> boundTextureByUnit;
  std::vector<float>
      pendingInstanceFloats; // from the latest setupInstanceAttributes

  // One entry per texture created, in creation order.
  std::vector<api::TextureDesc> textureDescs;
  std::vector<bool> textureHadAlpha;
  std::vector<std::pair<uint32_t, uint32_t>> textureSizes;
};

class FakeBuffer final : public api::IBuffer {
public:
  explicit FakeBuffer(int id = 0) : m_id(id) {
  }
  int id() const {
    return m_id;
  }
  void setData(const void *data, size_t sizeBytes, Usage) override {
    m_bytes.assign(static_cast<const unsigned char *>(data),
                   static_cast<const unsigned char *>(data) + sizeBytes);
  }
  void setSubData(size_t offset, const void *data, size_t sizeBytes) override {
    if (offset + sizeBytes > m_bytes.size()) {
      m_bytes.resize(offset + sizeBytes);
    }
    std::memcpy(m_bytes.data() + offset, data, sizeBytes);
  }
  size_t size() const override {
    return m_bytes.size();
  }
  void release() override {
  }
  const std::vector<unsigned char> &bytes() const {
    return m_bytes;
  }

private:
  int m_id;
  std::vector<unsigned char> m_bytes;
};

class FakeShader final : public api::IShader {
public:
  FakeShader(Recorder &rec, int id) : m_rec(rec), m_id(id) {
  }
  void loadGLSL(const char *, const char *fragmentSource,
                const char *) override {
    m_rec.fragmentSources[m_id] = fragmentSource ? fragmentSource : "";
  }
  void loadSPIRV(const char *, const char *, const char *) override {
  }
  void use() const override {
    m_rec.shaderUses.push_back(m_id);
    m_rec.currentShader = m_id;
  }
  void release() override {
  }

  void setFloat(const char *n, float v) const override {
    note(n);
    m_rec.floatUniforms[m_id][n] = v;
  }
  void setInteger(const char *n, int) const override {
    note(n);
  }
  void setVector2f(const char *n, float, float) const override {
    note(n);
  }
  void setVector3f(const char *n, float, float, float) const override {
    note(n);
  }
  void setVector4f(const char *n, float, float, float, float) const override {
    note(n);
  }
  void setMatrix4(const char *n, const float *) const override {
    note(n);
  }

  bool bindUniformBlock(const char *name, uint32_t binding, size_t) override {
    m_rec.uniformBlockBinds.push_back({name, binding});
    if (std::string(name) != "FrameData") {
      return m_rec.declaredBlocks.count(name) != 0;
    }
    auto it = m_rec.shaderDeclaresFrameBlock.find(m_id);
    return it != m_rec.shaderDeclaresFrameBlock.end()
               ? it->second
               : m_rec.defaultShaderDeclaresFrameBlock;
  }

  api::VertexLayout reflectInstanceLayout() const override {
    return m_rec.instanceLayout;
  }
  std::vector<componeng::core::Name>
  reflectActiveUniformNames() const override {
    return {};
  }

private:
  void note(const char *name) const {
    m_rec.looseUniforms[m_id].push_back(name);
  }
  Recorder &m_rec;
  int m_id;
};

class FakeTexture final : public api::ITexture {
public:
  FakeTexture(Recorder &rec, int id) : m_rec(rec), m_id(id) {
  }
  void generate(uint32_t w, uint32_t h, const unsigned char *, bool alpha,
                const api::TextureDesc &desc) override {
    m_width = w;
    m_height = h;
    m_rec.textureDescs.push_back(desc);
    m_rec.textureHadAlpha.push_back(alpha);
    m_rec.textureSizes.push_back({w, h});
  }
  void create(uint32_t w, uint32_t h, api::TextureFormat format,
              const void *pixels, const api::TextureDesc &desc) override {
    m_width = w;
    m_height = h;
    m_format = format;
    CreatedTexture t;
    t.id = m_id;
    t.width = w;
    t.height = h;
    t.format = format;
    t.desc = desc;
    t.hadPixels = pixels != nullptr;
    if (pixels) {
      const size_t bytes = size_t(w) * h * api::textureCpuPixelBytes(format);
      t.pixels.assign(static_cast<const unsigned char *>(pixels),
                      static_cast<const unsigned char *>(pixels) + bytes);
    }
    m_rec.createdTextures.push_back(std::move(t));
  }
  void update(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
              const void *pixels) override {
    TextureUpdate u;
    u.id = m_id;
    u.x = x;
    u.y = y;
    u.width = w;
    u.height = h;
    const size_t bytes = size_t(w) * h * api::textureCpuPixelBytes(m_format);
    u.pixels.assign(static_cast<const unsigned char *>(pixels),
                    static_cast<const unsigned char *>(pixels) + bytes);
    m_rec.textureUpdates.push_back(std::move(u));
  }
  void bind() const override {
    m_rec.boundTextureByUnit[m_rec.activeUnit] = m_id;
    if (m_rec.activeUnit == 0) {
      m_rec.unit0TextureBinds.push_back(m_id);
    }
  }
  void release() override {
  }

private:
  Recorder &m_rec;
  int m_id;
  api::TextureFormat m_format = api::TextureFormat::RGBA8;
};

class FakeRenderTarget final : public api::IRenderTarget {
public:
  FakeRenderTarget(Recorder &rec, int id, uint32_t w, uint32_t h)
      : m_id(id), m_color(rec, ++rec.texturesCreated) {
    m_width = w;
    m_height = h;
    CreatedTarget t;
    t.id = id;
    t.width = w;
    t.height = h;
    t.colorTextureId = rec.texturesCreated;
    rec.createdTargets.push_back(t);
  }
  int id() const {
    return m_id;
  }
  api::ITexture &colorTexture() override {
    return m_color;
  }

private:
  int m_id;
  FakeTexture m_color;
};

class FakeMesh final : public api::IMesh {
public:
  void upload(const float *, size_t, const uint32_t *, size_t indexCount,
              const api::VertexLayout &) override {
    m_indexCount = indexCount;
  }
  void bind() const override {
  }
  void release() override {
  }
  size_t indexCount() const override {
    return m_indexCount;
  }

private:
  size_t m_indexCount = 0;
};

class FakeRenderDevice final : public api::IRenderDevice {
public:
  Recorder rec;

  void init(void *) override {
  }
  void setViewport(int, int, int, int) override {
  }
  void setScissor(int, int, int, int) override {
  }
  api::Extent viewportExtent() const override {
    return rec.viewport;
  }
  void clear(float, float, float, float) override {
    ++rec.clears;
    rec.clearTargetIds.push_back(rec.currentTarget);
  }
  void present(void *) override {
  }
  int checkErrors() const override {
    return 0;
  }

  std::unique_ptr<api::IShader> createShader() override {
    return std::make_unique<FakeShader>(rec, ++rec.shadersCreated);
  }
  std::unique_ptr<api::ITexture> createTexture() override {
    return std::make_unique<FakeTexture>(rec, ++rec.texturesCreated);
  }
  std::unique_ptr<api::IMesh> createMesh() override {
    ++rec.meshesCreated;
    return std::make_unique<FakeMesh>();
  }
  std::unique_ptr<api::IBuffer> createBuffer() override {
    ++rec.buffersCreated;
    return std::make_unique<FakeBuffer>(rec.buffersCreated);
  }

  std::unique_ptr<api::IRenderTarget>
  createRenderTarget(const api::RenderTargetDesc &desc) override {
    return std::make_unique<FakeRenderTarget>(
        rec, static_cast<int>(rec.createdTargets.size()) + 1, desc.width,
        desc.height);
  }
  void setRenderTarget(api::IRenderTarget *target) override {
    rec.currentTarget =
        target ? static_cast<FakeRenderTarget *>(target)->id() : 0;
    rec.targetBinds.push_back(rec.currentTarget);
  }

  void setRenderState(const api::RenderState &state) override {
    rec.stateRequests.push_back(state);
    rec.currentState = state;
  }
  void bindUniformBuffer(uint32_t binding, api::IBuffer &buffer) override {
    rec.uniformBufferBinds.push_back(
        {binding, static_cast<FakeBuffer &>(buffer).bytes()});
  }
  void setActiveTextureUnit(uint32_t unit) override {
    rec.activeUnit = unit;
  }
  void bindBufferTexture(uint32_t unit, api::IBuffer &buffer,
                         api::BufferTextureFormat format) override {
    auto &fake = static_cast<FakeBuffer &>(buffer);
    rec.boundBufferTextureByUnit[unit] = fake.id();
    rec.bufferTextureBinds.push_back({unit, fake.id(), format, fake.bytes()});
  }
  void setupInstanceAttributes(api::IBuffer &buffer,
                               const api::VertexLayout &) override {
    const auto &bytes = static_cast<FakeBuffer &>(buffer).bytes();
    rec.pendingInstanceFloats.resize(bytes.size() / sizeof(float));
    std::memcpy(rec.pendingInstanceFloats.data(), bytes.data(),
                rec.pendingInstanceFloats.size() * sizeof(float));
  }
  void unbindInstanceAttributes(const api::VertexLayout &) override {
    // As in GL, once the arrays are disabled the next draw sees no instance
    // data, unless it binds its own.
    rec.pendingInstanceFloats.clear();
  }
  void beginGpuPass(api::PassId pass) override {
    rec.gpuPassEvents.push_back(std::string("begin ") + api::passName(pass));
  }
  void endGpuPass(api::PassId pass) override {
    rec.gpuPassEvents.push_back(std::string("end ") + api::passName(pass));
  }
  void drawFullscreenTriangle() override {
    Draw d;
    d.state = rec.currentState;
    d.shaderId = rec.currentShader;
    d.instanceCount = 1;
    d.textureUnits = rec.boundTextureByUnit;
    d.targetId = rec.currentTarget;
    rec.fullscreenDraws.push_back(d);
    rec.drawSequence.push_back("fullscreen");
    rec.gpuPassEvents.push_back("fullscreen");
  }
  void drawIndexedInstanced(size_t indexCount,
                            uint32_t instanceCount) override {
    Draw d;
    d.indexCount = indexCount;
    d.instanceCount = instanceCount;
    d.state = rec.currentState;
    d.instanceFloats = rec.pendingInstanceFloats;
    d.shaderId = rec.currentShader;
    auto it = rec.boundTextureByUnit.find(0);
    d.albedoTextureId = it != rec.boundTextureByUnit.end() ? it->second : 0;
    d.textureUnits = rec.boundTextureByUnit;
    d.bufferTextureUnits = rec.boundBufferTextureByUnit;
    d.targetId = rec.currentTarget;
    rec.draws.push_back(d);
    rec.drawSequence.push_back("indexed");
    rec.gpuPassEvents.push_back("indexed");
  }
};

} // namespace fake
