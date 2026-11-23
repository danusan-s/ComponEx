#pragma once

namespace componeng::core {

struct Vec3 {
  float x;
  float y;
  float z;

  constexpr Vec3() : x(0.0f), y(0.0f), z(0.0f) {
  }
  constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {
  }
  constexpr Vec3(float value) : x(value), y(value), z(value) {
  }

  Vec3 operator+(const Vec3 &other) const {
    return Vec3(x + other.x, y + other.y, z + other.z);
  }

  Vec3 operator-(const Vec3 &other) const {
    return Vec3(x - other.x, y - other.y, z - other.z);
  }

  Vec3 operator-() const {
    return Vec3(-x, -y, -z);
  }

  Vec3 operator*(float scalar) const {
    return Vec3(x * scalar, y * scalar, z * scalar);
  }

  Vec3 operator*(const Vec3 &other) const {
    return Vec3(x * other.x, y * other.y, z * other.z);
  }

  Vec3 operator/(float scalar) const {
    return Vec3(x / scalar, y / scalar, z / scalar);
  }

  bool operator==(const Vec3 &other) const {
    return x == other.x && y == other.y && z == other.z;
  }

  bool operator!=(const Vec3 &other) const {
    return !(*this == other);
  }

  Vec3 &operator+=(const Vec3 &other) {
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
  }

  Vec3 &operator-=(const Vec3 &other) {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
  }

  Vec3 &operator*=(float scalar) {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
  }

  Vec3 &operator/=(float scalar) {
    x /= scalar;
    y /= scalar;
    z /= scalar;
    return *this;
  }
};

inline Vec3 operator*(float scalar, const Vec3 &vec) {
  return vec * scalar;
}

} // namespace componeng::core
