#include "componeng/renderer/culling/frustum.hpp"
#include <array>

namespace componeng::renderer {
bool Frustum::isBoxInFrustum(const core::Vec3 &boxMin,
                             const core::Vec3 &boxMax) {
  for (int i = 0; i < 6; i++) {
    const FrustumPlane &plane = m_planes[i];
    core::Vec3 positiveVertex;
    positiveVertex.x = (plane.normal.x >= 0) ? boxMax.x : boxMin.x;
    positiveVertex.y = (plane.normal.y >= 0) ? boxMax.y : boxMin.y;
    positiveVertex.z = (plane.normal.z >= 0) ? boxMax.z : boxMin.z;

    if (dot(plane.normal, positiveVertex) + plane.distance < 0) {
      return false;
    }
  }
  return true;
}

Frustum::Frustum(const core::Mat4 &m) {

  m_planes[0].normal.x = m[0][3] + m[0][0];
  m_planes[0].normal.y = m[1][3] + m[1][0];
  m_planes[0].normal.z = m[2][3] + m[2][0];
  m_planes[0].distance = m[3][3] + m[3][0];

  m_planes[1].normal.x = m[0][3] - m[0][0];
  m_planes[1].normal.y = m[1][3] - m[1][0];
  m_planes[1].normal.z = m[2][3] - m[2][0];
  m_planes[1].distance = m[3][3] - m[3][0];

  m_planes[2].normal.x = m[0][3] + m[0][1];
  m_planes[2].normal.y = m[1][3] + m[1][1];
  m_planes[2].normal.z = m[2][3] + m[2][1];
  m_planes[2].distance = m[3][3] + m[3][1];

  m_planes[3].normal.x = m[0][3] - m[0][1];
  m_planes[3].normal.y = m[1][3] - m[1][1];
  m_planes[3].normal.z = m[2][3] - m[2][1];
  m_planes[3].distance = m[3][3] - m[3][1];

  m_planes[4].normal.x = m[0][3] + m[0][2];
  m_planes[4].normal.y = m[1][3] + m[1][2];
  m_planes[4].normal.z = m[2][3] + m[2][2];
  m_planes[4].distance = m[3][3] + m[3][2];

  m_planes[5].normal.x = m[0][3] - m[0][2];
  m_planes[5].normal.y = m[1][3] - m[1][2];
  m_planes[5].normal.z = m[2][3] - m[2][2];
  m_planes[5].distance = m[3][3] - m[3][2];

  for (int i = 0; i < 6; i++) {
    float len = core::length(m_planes[i].normal);
    float inv = 1.0f / len;
    m_planes[i].normal.x *= inv;
    m_planes[i].normal.y *= inv;
    m_planes[i].normal.z *= inv;
    m_planes[i].distance *= inv;
  }
}

std::array<FrustumPlane, 6> Frustum::getPlanes() {
  return m_planes;
}

} // namespace componeng::renderer
