#pragma once

#include <cmath>
#include <cstdint>

namespace forest::noise {

inline float hash(int x, int y, std::uint32_t seed = 0) {
  std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                    static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
}

inline float smooth(float t) {
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Value noise in [0, 1].
inline float value(float x, float y, std::uint32_t seed = 0) {
  float fx = std::floor(x), fy = std::floor(y);
  int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
  float tx = smooth(x - fx), ty = smooth(y - fy);
  float a = hash(ix, iy, seed), b = hash(ix + 1, iy, seed);
  float c = hash(ix, iy + 1, seed), d = hash(ix + 1, iy + 1, seed);
  return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

// Fractal sum of value noise, normalised to [0, 1]. Each octave is rotated and
// offset so the lattice axes do not line up into visible grid artefacts.
inline float fbm(float x, float y, int octaves, std::uint32_t seed = 0) {
  float sum = 0.0f, amp = 0.5f, norm = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * value(x, y, seed + static_cast<std::uint32_t>(i) * 31u);
    norm += amp;
    float nx = x * 1.6f + y * 1.2f + 17.3f;
    float ny = -x * 1.2f + y * 1.6f - 9.1f;
    x = nx;
    y = ny;
    amp *= 0.5f;
  }
  return sum / norm;
}

// Ridged noise in [0, 1]: sharp crests where fbm crosses 0.5.
inline float ridged(float x, float y, int octaves, std::uint32_t seed = 0) {
  float r = 1.0f - std::fabs(fbm(x, y, octaves, seed) * 2.0f - 1.0f);
  return r * r;
}

inline float smoothstep(float a, float b, float x) {
  float t = (x - a) / (b - a);
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return t * t * (3.0f - 2.0f * t);
}

} // namespace forest::noise
