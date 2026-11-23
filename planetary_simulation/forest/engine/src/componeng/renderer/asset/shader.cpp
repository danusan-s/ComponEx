#include "componeng/renderer/asset/shader.hpp"

#include "componeng/utils/logger.hpp"

#include <algorithm>
#include <vector>

namespace componeng::renderer {

// What the depth pre-pass runs for every fragment: nothing. The depth buffer
// is written by the rasteriser, not by the shader.
static constexpr const char *kDepthOnlyFragmentSource =
    "#version 330 core\nvoid main() {}\n";

std::string defineAfterVersion(const std::string &source, const char *name) {
  const std::string define = std::string("#define ") + name + " 1\n";
  // `#version` has to be the first directive, so the define goes after it.
  std::size_t version = source.find("#version");
  while (version != std::string::npos && version != 0 &&
         source[version - 1] != '\n') {
    version = source.find("#version", version + 1);
  }
  if (version == std::string::npos) {
    return define + source;
  }
  std::size_t lineEnd = source.find('\n', version);
  lineEnd = lineEnd == std::string::npos ? source.size() : lineEnd + 1;
  std::string out = source.substr(0, lineEnd);
  if (out.empty() || out.back() != '\n') {
    out += '\n';
  }
  return out + define + source.substr(lineEnd);
}

Shader::Shader(api::IRenderDevice &device)
    : m_device(&device), m_impl(device.createShader()) {
}

void Shader::use() const {
  m_impl->use();
}

void Shader::beginGpuTimer() const {
  m_impl->beginGpuTimer();
}

void Shader::endGpuTimer() const {
  m_impl->endGpuTimer();
}

void Shader::collectGpuTimer() const {
  m_impl->collectGpuTimer();
}

double Shader::lastGpuTimeMs() const {
  return m_impl->lastGpuTimeMs();
}

void Shader::compile(const char *vertexSource, const char *fragmentSource,
                     const char *geometrySource) {
  m_vertexSource = vertexSource;
  m_hasGeometry = geometrySource != nullptr;
  m_geometrySource = m_hasGeometry ? geometrySource : "";
  m_fragmentSource = fragmentSource ? fragmentSource : "";
  // Built from the sources that were just replaced.
  m_depthVariant.reset();
  m_alphaTestedDepthVariant.reset();
  m_impl->loadGLSL(vertexSource, fragmentSource, geometrySource);
  if (!m_impl->bindUniformBlock(kFrameDataBlockName, kFrameDataBinding,
                                sizeof(FrameData))) {
    warnIfLooseFrameUniforms();
  }
}

void Shader::warnIfLooseFrameUniforms() const {
  // A shader may skip the block when it needs no per-frame values. One that
  // reads them as plain uniforms would get zeros, since the engine only
  // uploads the block.
  static const char *const kFrameNames[] = {
      "viewProj", "cameraPos",  "lightDir", "lightColor", "ambientColor",
      "fogColor", "fogDensity", "fogStart", "fogEnd",     "time"};
  const std::vector<core::Name> active = m_impl->reflectActiveUniformNames();
  for (const char *name : kFrameNames) {
    const core::Name n(name);
    if (std::find(active.begin(), active.end(), n) != active.end()) {
      LOG_ERROR("Shader reads '%s' as a loose uniform, but the engine only "
                "uploads per-frame values in the %s block; declare it",
                name, kFrameDataBlockName);
    }
  }
}

void Shader::setFloat(const char *name, float value) const {
  m_impl->setFloat(name, value);
}

void Shader::setInteger(const char *name, int value) const {
  m_impl->setInteger(name, value);
}

void Shader::setVector2f(const char *name, float x, float y) const {
  m_impl->setVector2f(name, x, y);
}

void Shader::setVector2f(const char *name, const core::Vec2 &value) const {
  m_impl->setVector2f(name, value.x, value.y);
}

void Shader::setVector3f(const char *name, float x, float y, float z) const {
  m_impl->setVector3f(name, x, y, z);
}

void Shader::setVector3f(const char *name, const core::Vec3 &value) const {
  m_impl->setVector3f(name, value.x, value.y, value.z);
}

void Shader::setVector4f(const char *name, float x, float y, float z,
                         float w) const {
  m_impl->setVector4f(name, x, y, z, w);
}

void Shader::setVector4f(const char *name, const core::Vec4 &value) const {
  m_impl->setVector4f(name, value.x, value.y, value.z, value.w);
}

void Shader::setMatrix4(const char *name, const core::Mat4 &matrix) const {
  m_impl->setMatrix4(name, &matrix[0][0]);
}

bool Shader::bindUniformBlock(const char *blockName, uint32_t binding) const {
  return m_impl->bindUniformBlock(blockName, binding);
}

const Shader &Shader::depthVariant() const {
  if (!m_depthVariant) {
    m_depthVariant = std::make_unique<Shader>(*m_device);
    m_depthVariant->compile(m_vertexSource.c_str(), kDepthOnlyFragmentSource,
                            m_hasGeometry ? m_geometrySource.c_str() : nullptr);
  }
  return *m_depthVariant;
}

const Shader &Shader::alphaTestedDepthVariant() const {
  if (!m_alphaTestedDepthVariant) {
    m_alphaTestedDepthVariant = std::make_unique<Shader>(*m_device);
    m_alphaTestedDepthVariant->compile(
        m_vertexSource.c_str(),
        defineAfterVersion(m_fragmentSource, kDepthPrepassDefine).c_str(),
        m_hasGeometry ? m_geometrySource.c_str() : nullptr);
  }
  return *m_alphaTestedDepthVariant;
}

api::VertexLayout Shader::reflectInstanceLayout() const {
  return m_impl->reflectInstanceLayout();
}

std::vector<core::Name> Shader::reflectActiveUniformNames() const {
  return m_impl->reflectActiveUniformNames();
}

} // namespace componeng::renderer
