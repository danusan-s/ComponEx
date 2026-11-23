#pragma once

// Engine vector types as message fields (their floats in order).
// core::Name is deliberately excluded: reading one interns the string forever,
// so a peer could grow the table without bound. Send a std::string instead.

#include "componeng/core/math/vec2.hpp"
#include "componeng/core/math/vec3.hpp"
#include "componeng/core/math/vec4.hpp"

namespace componeng::core {

template <typename Ar> void serialize(Ar &ar, Vec2 &v) {
  ar(v.x, v.y);
}
template <typename Ar> void serialize(Ar &ar, Vec3 &v) {
  ar(v.x, v.y, v.z);
}
template <typename Ar> void serialize(Ar &ar, Vec4 &v) {
  ar(v.x, v.y, v.z, v.w);
}

} // namespace componeng::core
