#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "glad/glad.h"

#include <unordered_map>
#include <vector>

namespace componeng::renderer::opengl {

class GLShader final : public api::IShader {
public:
  GLShader() = default;
  ~GLShader() override;

  // Owns a GL handle freed in the dtor; copying it would double-free
  GLShader(const GLShader &) = delete;
  GLShader &operator=(const GLShader &) = delete;
  GLShader(GLShader &&) = delete;
  GLShader &operator=(GLShader &&) = delete;

  void loadGLSL(const char *vertexSource, const char *fragmentSource,
                const char *geometrySource = nullptr) override;
  void loadSPIRV(const char *vertexPath, const char *fragmentPath,
                 const char *geometryPath = nullptr) override;
  void use() const override;
  void release() override;

  void beginGpuTimer() const override;
  void endGpuTimer() const override;
  void collectGpuTimer() const override;
  double lastGpuTimeMs() const override {
    return m_gpuTimeMs;
  }

  void setFloat(const char *name, float value) const override;
  void setInteger(const char *name, int value) const override;
  void setVector2f(const char *name, float x, float y) const override;
  void setVector3f(const char *name, float x, float y, float z) const override;
  void setVector4f(const char *name, float x, float y, float z,
                   float w) const override;
  void setMatrix4(const char *name, const float *matrix) const override;

  bool bindUniformBlock(const char *blockName, uint32_t binding,
                        size_t expectedSizeBytes = 0) override;

  api::VertexLayout reflectInstanceLayout() const override;
  std::vector<core::Name> reflectActiveUniformNames() const override;

  GLuint handle() const {
    return m_id;
  }

private:
  void checkCompileErrors(GLuint object, const std::string &type);
  GLint getUniformLocation(const char *name) const;

  GLuint m_id = 0;
  mutable std::unordered_map<core::Name, GLint> m_uniformLocationCache;

  // GPU timer: one slot per in-flight frame. Each timed span is a pair of
  // GL_TIMESTAMP queries (start, end), because GL_TIME_ELAPSED cannot be used
  // here: the device's whole-frame timer keeps one active and they cannot
  // nest. A slot's result is only read once the GPU reports it available, so
  // reading never stalls.
  struct GpuSlot {
    std::vector<GLuint> queries; // 2 per span: [start0, end0, start1, ...]
    int spans = 0;
    bool pending = false; // frame closed, result not read yet
  };
  static constexpr int kGpuSlotCount = 4;
  mutable GpuSlot m_gpuSlots[kGpuSlotCount];
  mutable int m_gpuCurrent = 0; // slot the frame in progress records to
  mutable bool m_gpuSpanOpen =
      false; // begin() issued a start that end() closes
  mutable double m_gpuTimeMs = -1.0;
  void releaseGpuQueries();
};

} // namespace componeng::renderer::opengl
