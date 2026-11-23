#pragma once

#include "vec3.hpp"

namespace componeng::core {

struct Vec4 {
  float x;
  float y;
  float z;
  float w;

  constexpr Vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {
  }
  constexpr Vec4(float x_, float y_, float z_, float w_)
      : x(x_), y(y_), z(z_), w(w_) {
  }
  constexpr Vec4(float value) : x(value), y(value), z(value), w(value) {
  }

  Vec4(const Vec3 &vec3, float w_) : x(vec3.x), y(vec3.y), z(vec3.z), w(w_) {
  }

  Vec4 operator+(const Vec4 &other) const {
    return Vec4(x + other.x, y + other.y, z + other.z, w + other.w);
  }

  Vec4 operator-(const Vec4 &other) const {
    return Vec4(x - other.x, y - other.y, z - other.z, w - other.w);
  }

  Vec4 operator*(float scalar) const {
    return Vec4(x * scalar, y * scalar, z * scalar, w * scalar);
  }

  Vec4 operator/(float scalar) const {
    return Vec4(x / scalar, y / scalar, z / scalar, w / scalar);
  }

  Vec4 &operator+=(const Vec4 &other) {
    x += other.x;
    y += other.y;
    z += other.z;
    w += other.w;
    return *this;
  }

  Vec4 &operator-=(const Vec4 &other) {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    w -= other.w;
    return *this;
  }
};

} // namespace componeng::core
