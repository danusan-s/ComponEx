#include "componeng/renderer/backend/opengl/gl_mesh.hpp"

#include "componeng/utils/logger.hpp"

namespace componeng::renderer::opengl {

GLMesh::~GLMesh() {
  release();
}

void GLMesh::upload(const float *vertices, size_t vertexCount,
                    const uint32_t *indices, size_t indexCount,
                    const api::VertexLayout &layout) {
  if (m_vao != 0) {
    glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
  }
  if (m_vbo != 0) {
    glDeleteBuffers(1, &m_vbo);
    m_vbo = 0;
  }
  if (m_ebo != 0) {
    glDeleteBuffers(1, &m_ebo);
    m_ebo = 0;
  }

  m_indexCount = indexCount;

  glGenVertexArrays(1, &m_vao);
  glGenBuffers(1, &m_vbo);
  glGenBuffers(1, &m_ebo);

  glBindVertexArray(m_vao);

  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glBufferData(GL_ARRAY_BUFFER, vertexCount * layout.stride, vertices,
               GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(uint32_t), indices,
               GL_STATIC_DRAW);

  for (size_t i = 0; i < layout.attributes.size(); ++i) {
    const auto &attr = layout.attributes[i];
    glVertexAttribPointer(i, attr.componentCount, GL_FLOAT, attr.normalized,
                          layout.stride,
                          reinterpret_cast<const void *>(attr.offset));
    glEnableVertexAttribArray(i);
  }

  glBindVertexArray(0);
}

void GLMesh::bind() const {
  glBindVertexArray(m_vao);
}

void GLMesh::release() {
  if (m_vao != 0) {
    glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
  }
  if (m_vbo != 0) {
    glDeleteBuffers(1, &m_vbo);
    m_vbo = 0;
  }
  if (m_ebo != 0) {
    glDeleteBuffers(1, &m_ebo);
    m_ebo = 0;
  }
  m_indexCount = 0;
}

size_t GLMesh::indexCount() const {
  return m_indexCount;
}

} // namespace componeng::renderer::opengl
