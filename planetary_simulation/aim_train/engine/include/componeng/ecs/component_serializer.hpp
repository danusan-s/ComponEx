#pragma once

#include "componeng/core/types.hpp"
#include "componeng/utils/logger.hpp"
#include "json.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace componeng::ecs {

template <typename T> struct ComponentSerializer {
  static nlohmann::json serialize(const T &) {
    LOG_ERROR("Serialization not implemented for this component type. %s",
              typeid(T).name());
    return nlohmann::json();
  }

  static T deserialize(const nlohmann::json &) {
    LOG_ERROR("Deserialization not implemented for this component type. %s",
              typeid(T).name());
    return T();
  }
};

static inline std::array<float, 16> SerializeMat4(const core::Mat4 &m) {
  std::array<float, 16> data;
  const float *ptr = &m[0][0]; // GLM guarantees contiguous storage
  for (int i = 0; i < 16; i++)
    data[i] = ptr[i];
  return data;
}

static inline core::Mat4 DeserializeMat4(const std::vector<float> &data) {
  if (data.size() != 16)
    throw std::runtime_error("Invalid data size for deserializing glm::mat4");
  core::Mat4 m;
  float *ptr = &m[0][0];
  for (int i = 0; i < 16; i++)
    ptr[i] = data[i];
  return m;
}

// --- Type detection traits ---
template <typename T> struct is_vec2 : std::false_type {};
template <> struct is_vec2<core::Vec2> : std::true_type {};

template <typename T> struct is_vec3 : std::false_type {};
template <> struct is_vec3<core::Vec3> : std::true_type {};

template <typename T> struct is_vec4 : std::false_type {};
template <> struct is_vec4<core::Vec4> : std::true_type {};

template <typename T> struct is_mat4 : std::false_type {};
template <> struct is_mat4<core::Mat4> : std::true_type {};

// --- Generic field serialization dispatcher (zero macros) ---
template <typename T, typename M>
void serialize_field(nlohmann::json &j, const char *name, const T &obj,
                     M T::*member) {
  const auto &val = obj.*member;
  using MT = std::decay_t<M>;

  if constexpr (std::is_same_v<MT, float>) {
    j[name] = val;
  } else if constexpr (std::is_same_v<MT, int>) {
    j[name] = val;
  } else if constexpr (std::is_same_v<MT, bool>) {
    j[name] = val;
  } else if constexpr (std::is_same_v<MT, std::string>) {
    j[name] = val;
  } else if constexpr (std::is_same_v<MT, core::Name>) {
    j[name] = val.c_str();
  } else if constexpr (is_vec3<MT>::value) {
    j[name] = {val.x, val.y, val.z};
  } else if constexpr (is_vec4<MT>::value) {
    j[name] = {val.x, val.y, val.z, val.w};
  } else if constexpr (is_mat4<MT>::value) {
    j[name] = SerializeMat4(val);
  } else if constexpr (std::is_enum_v<MT>) {
    j[name] = static_cast<int>(val);
  } else {
    j[name] = ComponentSerializer<MT>::serialize(val);
  }
}

template <typename T, typename M>
void deserialize_field(const nlohmann::json &j, const char *name, T &obj,
                       M T::*member) {
  auto &val = obj.*member;
  using MT = std::decay_t<M>;

  if constexpr (std::is_same_v<MT, float>) {
    val = j.value(name, val);
  } else if constexpr (std::is_same_v<MT, int>) {
    val = j.value(name, val);
  } else if constexpr (std::is_same_v<MT, bool>) {
    val = j.value(name, val);
  } else if constexpr (std::is_same_v<MT, std::string>) {
    val = j.value(name, val);
  } else if constexpr (std::is_same_v<MT, core::Name>) {
    std::string defaultStr(val.c_str());
    val = core::Name(j.value(name, defaultStr).c_str());
  } else if constexpr (is_vec3<MT>::value) {
    if (j.contains(name) && j[name].is_array() && j[name].size() == 3) {
      val.x = j[name][0];
      val.y = j[name][1];
      val.z = j[name][2];
    }
  } else if constexpr (is_vec4<MT>::value) {
    if (j.contains(name) && j[name].is_array() && j[name].size() == 4) {
      val.x = j[name][0];
      val.y = j[name][1];
      val.z = j[name][2];
      val.w = j[name][3];
    }
  } else if constexpr (is_mat4<MT>::value) {
    if (j.contains(name) && j[name].is_array()) {
      val = DeserializeMat4(j[name].get<std::vector<float>>());
    }
  } else if constexpr (std::is_enum_v<MT>) {
    val = static_cast<MT>(j.value(name, static_cast<int>(val)));
  } else {
    if (j.contains(name)) {
      val = ComponentSerializer<MT>::deserialize(j[name]);
    }
  }
}

// --- Macro infrastructure (3 macros total) ---
// Each component defines a field list macro:
//   #define MY_COMP_FIELDS(F, ctx)  F(field1, ctx)  F(field2, ctx) ...
// Then: SERIALIZABLE_COMPONENT(MyComp, MY_COMP_FIELDS)
// Type and default are deduced from pointer-to-member and member initializers.

#define SERIALIZE_FIELD(name, ctx)                                             \
  ::componeng::ecs::serialize_field(j, #name, ctx,                             \
                                    &std::decay_t<decltype(ctx)>::name);

#define DESERIALIZE_FIELD(name, ctx)                                           \
  ::componeng::ecs::deserialize_field(j, #name, ctx,                           \
                                      &std::decay_t<decltype(ctx)>::name);

#define SERIALIZABLE_COMPONENT(Type, Fields)                                   \
  template <> struct ComponentSerializer<Type> {                               \
    static nlohmann::json serialize(const Type &component) {                   \
      nlohmann::json j;                                                        \
      Fields(SERIALIZE_FIELD, component) return j;                             \
    }                                                                          \
    static Type deserialize(const nlohmann::json &j) {                         \
      Type component;                                                          \
      Fields(DESERIALIZE_FIELD, component) return component;                   \
    }                                                                          \
  };

} // namespace componeng::ecs
