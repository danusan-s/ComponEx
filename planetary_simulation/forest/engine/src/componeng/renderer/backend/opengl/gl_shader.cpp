#include "componeng/renderer/backend/opengl/gl_shader.hpp"

#include "componeng/utils/logger.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace componeng::renderer::opengl {

GLShader::~GLShader() {
  release();
}

void GLShader::loadGLSL(const char *vertexSource, const char *fragmentSource,
                        const char *geometrySource) {
  // Any cached locations belong to the program we're about to delete.
  m_uniformLocationCache.clear();

  if (m_id != 0) {
    glDeleteProgram(m_id);
    m_id = 0;
  }

  GLuint sVertex = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(sVertex, 1, &vertexSource, nullptr);
  glCompileShader(sVertex);
  checkCompileErrors(sVertex, "VERTEX");

  GLuint sFragment = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(sFragment, 1, &fragmentSource, nullptr);
  glCompileShader(sFragment);
  checkCompileErrors(sFragment, "FRAGMENT");

  GLuint gShader = 0;
  if (geometrySource != nullptr) {
    gShader = glCreateShader(GL_GEOMETRY_SHADER);
    glShaderSource(gShader, 1, &geometrySource, nullptr);
    glCompileShader(gShader);
    checkCompileErrors(gShader, "GEOMETRY");
  }

  m_id = glCreateProgram();
  glAttachShader(m_id, sVertex);
  glAttachShader(m_id, sFragment);
  if (geometrySource != nullptr)
    glAttachShader(m_id, gShader);
  glLinkProgram(m_id);
  checkCompileErrors(m_id, "PROGRAM");

  glDeleteShader(sVertex);
  glDeleteShader(sFragment);
  if (geometrySource != nullptr)
    glDeleteShader(gShader);
}

void GLShader::loadSPIRV(const char *vertexPath, const char *fragmentPath,
                         const char *geometryPath) {
  // OpenGL doesn't natively support SPIR-V without GL_ARB_gl_spirv extension.
  // Fall back to loading GLSL source by changing .spv extension to .vert/.frag
  auto stripExt = [](const char *path) -> std::string {
    std::string s(path);
    auto dot = s.find_last_of('.');
    if (dot != std::string::npos) {
      // Remove .spv suffix
      if (s.size() >= 4 && s.substr(dot) == ".spv") {
        s.erase(dot);
      }
    }
    return s;
  };

  std::string vertPath = stripExt(vertexPath);
  std::string fragPath = stripExt(fragmentPath);
  std::string geomPath;
  if (geometryPath != nullptr) {
    geomPath = stripExt(geometryPath);
  }

  auto readFile = [](const std::string &path) -> std::string {
    std::ifstream file(path);
    if (!file.is_open()) {
      LOG_ERROR("Failed to open shader file: %s", path.c_str());
      return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
  };

  std::string vertSrc = readFile(vertPath);
  std::string fragSrc = readFile(fragPath);
  std::string geomSrc;
  if (geometryPath != nullptr && !geomPath.empty()) {
    geomSrc = readFile(geomPath);
  }

  if (!vertSrc.empty() && !fragSrc.empty()) {
    loadGLSL(vertSrc.c_str(), fragSrc.c_str(),
             geometryPath != nullptr && !geomSrc.empty() ? geomSrc.c_str()
                                                         : nullptr);
  }
}

void GLShader::use() const {
  glUseProgram(m_id);
}

void GLShader::release() {
  releaseGpuQueries();
  if (m_id != 0) {
    glDeleteProgram(m_id);
    m_id = 0;
  }
}

void GLShader::releaseGpuQueries() {
  for (GpuSlot &slot : m_gpuSlots) {
    if (!slot.queries.empty()) {
      glDeleteQueries(static_cast<GLsizei>(slot.queries.size()),
                      slot.queries.data());
    }
    slot = GpuSlot{};
  }
  m_gpuCurrent = 0;
  m_gpuSpanOpen = false;
  m_gpuTimeMs = -1.0;
}

void GLShader::beginGpuTimer() const {
  if (m_gpuSpanOpen)
    return; // already timing; spans do not nest
  GpuSlot &slot = m_gpuSlots[m_gpuCurrent];
  // Every slot is still waiting on the GPU: skip this span rather than
  // overwrite queries that are in flight.
  if (slot.pending)
    return;

  const std::size_t needed = static_cast<std::size_t>(slot.spans + 1) * 2;
  if (slot.queries.size() < needed) {
    const std::size_t have = slot.queries.size();
    slot.queries.resize(needed);
    glGenQueries(static_cast<GLsizei>(needed - have), &slot.queries[have]);
  }
  glQueryCounter(slot.queries[static_cast<std::size_t>(slot.spans) * 2],
                 GL_TIMESTAMP);
  m_gpuSpanOpen = true;
}

void GLShader::endGpuTimer() const {
  if (!m_gpuSpanOpen)
    return;
  GpuSlot &slot = m_gpuSlots[m_gpuCurrent];
  glQueryCounter(slot.queries[static_cast<std::size_t>(slot.spans) * 2 + 1],
                 GL_TIMESTAMP);
  ++slot.spans;
  m_gpuSpanOpen = false;
}

void GLShader::collectGpuTimer() const {
  endGpuTimer(); // a span left open belongs to this frame

  // Close the frame. A frame in which the shader was unused is closed too
  // (zero spans), so its 0 ms is reported in order, after older frames.
  GpuSlot &current = m_gpuSlots[m_gpuCurrent];
  if (!current.pending) {
    current.pending = true;
    m_gpuCurrent = (m_gpuCurrent + 1) % kGpuSlotCount;
  }

  // Collect finished frames, oldest first, stopping at the first one the GPU
  // has not finished so results are never reported out of order.
  for (int i = 0; i < kGpuSlotCount; ++i) {
    GpuSlot &slot = m_gpuSlots[(m_gpuCurrent + i) % kGpuSlotCount];
    if (!slot.pending)
      continue;
    if (slot.spans > 0) {
      // Timestamps complete in submission order, so the last end implies all.
      GLint available = 0;
      glGetQueryObjectiv(
          slot.queries[static_cast<std::size_t>(slot.spans) * 2 - 1],
          GL_QUERY_RESULT_AVAILABLE, &available);
      if (!available)
        break;
    }
    GLuint64 nanoseconds = 0;
    for (int s = 0; s < slot.spans; ++s) {
      GLuint64 start = 0;
      GLuint64 end = 0;
      glGetQueryObjectui64v(slot.queries[static_cast<std::size_t>(s) * 2],
                            GL_QUERY_RESULT, &start);
      glGetQueryObjectui64v(slot.queries[static_cast<std::size_t>(s) * 2 + 1],
                            GL_QUERY_RESULT, &end);
      nanoseconds += end - start;
    }
    m_gpuTimeMs = static_cast<double>(nanoseconds) * 1e-6;
    slot.spans = 0;
    slot.pending = false;
  }
}

GLint GLShader::getUniformLocation(const char *name) const {
  auto it = m_uniformLocationCache.find(name);
  if (it != m_uniformLocationCache.end()) {
    return it->second;
  }
  GLint location = glGetUniformLocation(m_id, name);
  m_uniformLocationCache.emplace(name, location);
  return location;
}

void GLShader::setFloat(const char *name, float value) const {
  glUniform1f(getUniformLocation(name), value);
}

void GLShader::setInteger(const char *name, int value) const {
  glUniform1i(getUniformLocation(name), value);
}

void GLShader::setVector2f(const char *name, float x, float y) const {
  glUniform2f(getUniformLocation(name), x, y);
}

void GLShader::setVector3f(const char *name, float x, float y, float z) const {
  glUniform3f(getUniformLocation(name), x, y, z);
}

void GLShader::setVector4f(const char *name, float x, float y, float z,
                           float w) const {
  glUniform4f(getUniformLocation(name), x, y, z, w);
}

void GLShader::setMatrix4(const char *name, const float *matrix) const {
  glUniformMatrix4fv(getUniformLocation(name), 1, false, matrix);
}

api::VertexLayout GLShader::reflectInstanceLayout() const {
  GLint activeAttribs = 0;
  glGetProgramiv(m_id, GL_ACTIVE_ATTRIBUTES, &activeAttribs);

  GLint maxNameLen = 0;
  glGetProgramiv(m_id, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &maxNameLen);
  std::vector<char> nameBuf(maxNameLen > 0 ? maxNameLen : 1);

  struct RawAttrib {
    std::string name;
    GLenum type;
    GLint location;
  };
  std::vector<RawAttrib> raw;

  for (GLint i = 0; i < activeAttribs; ++i) {
    GLsizei length = 0;
    GLint size = 0;
    GLenum type = 0;
    glGetActiveAttrib(m_id, i, static_cast<GLsizei>(nameBuf.size()), &length,
                      &size, &type, nameBuf.data());
    GLint location = glGetAttribLocation(m_id, nameBuf.data());
    if (location < static_cast<GLint>(api::kFirstInstanceAttribLocation)) {
      continue; // Mesh attribute (position/normal/uv), not instance data.
    }
    raw.push_back({std::string(nameBuf.data(), length), type, location});
  }

  std::sort(raw.begin(), raw.end(), [](const RawAttrib &a, const RawAttrib &b) {
    return a.location < b.location;
  });

  api::VertexLayout layout;
  uint32_t offset = 0;
  for (const auto &attrib : raw) {
    switch (attrib.type) {
      case GL_FLOAT:
        layout.attributes.push_back({attrib.name, offset, 1, false, -1});
        offset += sizeof(float);
        break;
      case GL_FLOAT_VEC2:
        layout.attributes.push_back({attrib.name, offset, 2, false, -1});
        offset += sizeof(float) * 2;
        break;
      case GL_FLOAT_VEC3:
        layout.attributes.push_back({attrib.name, offset, 3, false, -1});
        offset += sizeof(float) * 3;
        break;
      case GL_FLOAT_VEC4:
        layout.attributes.push_back({attrib.name, offset, 4, false, -1});
        offset += sizeof(float) * 4;
        break;
      case GL_FLOAT_MAT4:
        for (int row = 0; row < 4; ++row) {
          layout.attributes.push_back({attrib.name, offset, 4, false, row});
          offset += sizeof(float) * 4;
        }
        break;
      default:
        LOG_ERROR("Unsupported instance attribute type for '%s'",
                  attrib.name.c_str());
        break;
    }
  }
  layout.stride = offset;
  return layout;
}

bool GLShader::bindUniformBlock(const char *blockName, uint32_t binding,
                                size_t expectedSizeBytes) {
  const GLuint index = glGetUniformBlockIndex(m_id, blockName);
  if (index == GL_INVALID_INDEX) {
    return false;
  }

  if (expectedSizeBytes != 0) {
    GLint actualSize = 0;
    glGetActiveUniformBlockiv(m_id, index, GL_UNIFORM_BLOCK_DATA_SIZE,
                              &actualSize);
    if (static_cast<size_t>(actualSize) != expectedSizeBytes) {
      LOG_ERROR("Uniform block '%s' is %d bytes in the shader but %zu on the "
                "CPU; the declaration and the C++ struct disagree",
                blockName, actualSize, expectedSizeBytes);
    }
  }

  glUniformBlockBinding(m_id, index, binding);
  return true;
}

std::vector<core::Name> GLShader::reflectActiveUniformNames() const {
  GLint activeUniforms = 0;
  glGetProgramiv(m_id, GL_ACTIVE_UNIFORMS, &activeUniforms);

  GLint maxNameLen = 0;
  glGetProgramiv(m_id, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxNameLen);
  std::vector<char> nameBuf(maxNameLen > 0 ? maxNameLen : 1);

  std::vector<core::Name> names;
  for (GLint i = 0; i < activeUniforms; ++i) {
    GLsizei length = 0;
    GLint size = 0;
    GLenum type = 0;
    glGetActiveUniform(m_id, i, static_cast<GLsizei>(nameBuf.size()), &length,
                       &size, &type, nameBuf.data());

    // Members of a uniform block are not set through glUniform*; they live in
    // a buffer. Leave them out so setUniform() flags an attempt to set one.
    const GLuint uniformIndex = static_cast<GLuint>(i);
    GLint blockIndex = -1;
    glGetActiveUniformsiv(m_id, 1, &uniformIndex, GL_UNIFORM_BLOCK_INDEX,
                          &blockIndex);
    if (blockIndex != -1) {
      continue;
    }

    names.emplace_back(std::string(nameBuf.data(), length));
  }
  return names;
}

void GLShader::checkCompileErrors(GLuint object, const std::string &type) {
  int success;
  char infoLog[1024];
  if (type != "PROGRAM") {
    glGetShaderiv(object, GL_COMPILE_STATUS, &success);
    if (!success) {
      glGetShaderInfoLog(object, 1024, nullptr, infoLog);
      LOG_ERROR("ERROR::SHADER: Compile-time error: Type: %s\n%s", type.c_str(),
                infoLog);
    }
  } else {
    glGetProgramiv(object, GL_LINK_STATUS, &success);
    if (!success) {
      glGetProgramInfoLog(object, 1024, nullptr, infoLog);
      LOG_ERROR("ERROR::Shader: Link-time error: Type: %s\n%s", type.c_str(),
                infoLog);
    }
  }
}

} // namespace componeng::renderer::opengl
