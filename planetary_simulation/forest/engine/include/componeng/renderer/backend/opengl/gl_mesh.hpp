#pragma once

#include "componeng/renderer/backend/api/irender_device.hpp"
#include "glad/glad.h"

namespace componeng::renderer::opengl {

class GLMesh final : public api::IMesh {
public:
  GLMesh() = default;
  ~GLMesh() override;

  // Owns GL handles freed in the dtor; copying it would double-free
  GLMesh(const GLMesh &) = delete;
  GLMesh &operator=(const GLMesh &) = delete;
  GLMesh(GLMesh &&) = delete;
  GLMesh &operator=(GLMesh &&) = delete;

  void upload(const float *vertices, size_t vertexCount,
              const uint32_t *indices, size_t indexCount,
              const api::VertexLayout &layout) override;
  void bind() const override;
  void release() override;
  size_t indexCount() const override;

  GLuint vao() const {
    return m_vao;
  }

private:
  GLuint m_vao = 0;
  GLuint m_vbo = 0;
  GLuint m_ebo = 0;
  size_t m_indexCount = 0;
};

} // namespace componeng::renderer::opengl
