#pragma once

#include "componeng/core/math/mat4.hpp"
#include "componeng/core/math/vec2.hpp"
#include "componeng/core/math/vec3.hpp"
#include "componeng/core/math/vec4.hpp"

#include <cmath>

namespace componeng::core {

constexpr float PI = 3.14159265358979323846f;

inline float length(const Vec3 &v) {
  return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline float dot(const Vec3 &a, const Vec3 &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 normalize(const Vec3 &v) {
  float len = length(v);
  if (len > 0.0f) {
    float inv = 1.0f / len;
    return Vec3(v.x * inv, v.y * inv, v.z * inv);
  }
  return Vec3(std::nanf(""), std::nanf(""), std::nanf(""));
}

inline Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x);
}

inline Mat4 lookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up) {
  Vec3 f = normalize(center - eye);
  Vec3 s = normalize(cross(f, up));
  Vec3 u = cross(s, f);

  Mat4 result(1.0f);
  result[0][0] = s.x;
  result[1][0] = s.y;
  result[2][0] = s.z;
  result[0][1] = u.x;
  result[1][1] = u.y;
  result[2][1] = u.z;
  result[0][2] = -f.x;
  result[1][2] = -f.y;
  result[2][2] = -f.z;
  result[3][0] = -dot(s, eye);
  result[3][1] = -dot(u, eye);
  result[3][2] = dot(f, eye);
  return result;
}

inline Mat4 perspective(float fov, float aspect, float nearPlane,
                        float farPlane) {
  float invTan = 1.0f / std::tan(fov / 2.0f);
  Mat4 result(0.0f);
  result[0][0] = invTan / aspect;
  result[1][1] = invTan;
  result[2][2] = -(farPlane + nearPlane) / (farPlane - nearPlane);
  result[2][3] = -1.0f;
  result[3][2] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
  return result;
}

inline Mat4 translate(const Mat4 &matrix, const Vec3 &translation) {
  Mat4 result = matrix;
  result[3][0] = matrix[0][0] * translation.x + matrix[1][0] * translation.y +
                 matrix[2][0] * translation.z + matrix[3][0];
  result[3][1] = matrix[0][1] * translation.x + matrix[1][1] * translation.y +
                 matrix[2][1] * translation.z + matrix[3][1];
  result[3][2] = matrix[0][2] * translation.x + matrix[1][2] * translation.y +
                 matrix[2][2] * translation.z + matrix[3][2];
  result[3][3] = matrix[0][3] * translation.x + matrix[1][3] * translation.y +
                 matrix[2][3] * translation.z + matrix[3][3];
  return result;
}

inline Mat4 rotate(const Mat4 &matrix, float angle, const Vec3 &axis) {
  float c = std::cos(angle);
  float s = std::sin(angle);
  float t = 1.0f - c;
  float x = axis.x, y = axis.y, z = axis.z;
  if (x * x + y * y + z * z != 1.0f) {
    Vec3 n = normalize(axis);
    x = n.x;
    y = n.y;
    z = n.z;
  }
  float r00 = c + t * x * x;
  float r01 = t * x * y + s * z;
  float r02 = t * x * z - s * y;
  float r10 = t * y * x - s * z;
  float r11 = c + t * y * y;
  float r12 = t * y * z + s * x;
  float r20 = t * z * x + s * y;
  float r21 = t * z * y - s * x;
  float r22 = c + t * z * z;

  Mat4 result;
  for (int row = 0; row < 4; ++row) {
    float m0 = matrix[0][row], m1 = matrix[1][row], m2 = matrix[2][row];
    result[0][row] = m0 * r00 + m1 * r01 + m2 * r02;
    result[1][row] = m0 * r10 + m1 * r11 + m2 * r12;
    result[2][row] = m0 * r20 + m1 * r21 + m2 * r22;
    result[3][row] = matrix[3][row];
  }
  return result;
}

inline Mat4 scale(const Mat4 &matrix, const Vec3 &scale) {
  Mat4 result = matrix;
  for (int r = 0; r < 4; ++r) {
    result[0][r] *= scale.x;
    result[1][r] *= scale.y;
    result[2][r] *= scale.z;
  }
  return result;
}

inline Mat4 transpose(const Mat4 &matrix) {
  Mat4 result;
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      result[c][r] = matrix[r][c];
    }
  }
  return result;
}

/**
 * @brief General 4x4 inverse by cofactor expansion.
 *
 * Returns the identity matrix for a singular input (determinant 0) rather
 * than a matrix full of infinities, so callers that unproject with it degrade
 * to a no-op instead of poisoning everything downstream with NaNs.
 */
inline Mat4 inverse(const Mat4 &matrix) {
  const float *m = &matrix[0][0]; // column-major, m[col * 4 + row]
  float inv[16];

  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
           m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
           m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
           m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
           m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
           m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
           m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
           m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
           m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
           m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
           m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  const float det =
      m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (det == 0.0f) {
    return Mat4(1.0f);
  }

  const float invDet = 1.0f / det;
  Mat4 result;
  float *out = &result[0][0];
  for (int i = 0; i < 16; ++i) {
    out[i] = inv[i] * invDet;
  }
  return result;
}

inline constexpr float radians(float degrees) {
  return degrees * (PI / 180.0f);
}

inline const float *value_ptr(const Vec2 &v) {
  return &v.x;
}

inline const float *value_ptr(const Vec3 &v) {
  return &v.x;
}

inline const float *value_ptr(const Vec4 &v) {
  return &v.x;
}

inline const float *value_ptr(const Mat4 &m) {
  return &m[0][0];
}

} // namespace componeng::core
