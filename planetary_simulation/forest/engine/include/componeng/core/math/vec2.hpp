#pragma once

namespace componeng::core {

struct Vec2 {
  float x;
  float y;

  constexpr Vec2() : x(0.0f), y(0.0f) {
  }
  constexpr Vec2(float x_, float y_) : x(x_), y(y_) {
  }
  constexpr Vec2(float value) : x(value), y(value) {
  }

  Vec2 operator+(const Vec2 &other) const {
    return Vec2(x + other.x, y + other.y);
  }

  Vec2 operator-(const Vec2 &other) const {
    return Vec2(x - other.x, y - other.y);
  }

  Vec2 operator*(float scalar) const {
    return Vec2(x * scalar, y * scalar);
  }

  Vec2 operator/(float scalar) const {
    return Vec2(x / scalar, y / scalar);
  }

  Vec2 &operator+=(const Vec2 &other) {
    x += other.x;
    y += other.y;
    return *this;
  }

  Vec2 &operator-=(const Vec2 &other) {
    x -= other.x;
    y -= other.y;
    return *this;
  }
};

} // namespace componeng::core
