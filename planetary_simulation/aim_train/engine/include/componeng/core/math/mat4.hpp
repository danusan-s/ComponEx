#pragma once

namespace componeng::core {

struct alignas(16) Mat4 {
  float m[4][4];

  constexpr Mat4()
      : m{{1.0f, 0.0f, 0.0f, 0.0f},
          {0.0f, 1.0f, 0.0f, 0.0f},
          {0.0f, 0.0f, 1.0f, 0.0f},
          {0.0f, 0.0f, 0.0f, 1.0f}} {
  }

  constexpr Mat4(float diagonal)
      : m{{diagonal, 0.0f, 0.0f, 0.0f},
          {0.0f, diagonal, 0.0f, 0.0f},
          {0.0f, 0.0f, diagonal, 0.0f},
          {0.0f, 0.0f, 0.0f, diagonal}} {
  }

  float *operator[](int index) {
    return m[index];
  }

  const float *operator[](int index) const {
    return m[index];
  }

  Mat4 operator*(const Mat4 &other) const {
    Mat4 result(0.0f);
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        for (int k = 0; k < 4; ++k) {
          result.m[i][j] += m[k][j] * other.m[i][k];
        }
      }
    }
    return result;
  }
};

} // namespace componeng::core
