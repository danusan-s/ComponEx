#pragma once

#include "componeng/core/transform_component.hpp"
#include "componeng/core/types.hpp"
#include "componeng/ecs/entity.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace componeng::ecs {
class World;
} // namespace componeng::ecs

namespace componeng::renderer {

/**
 * @brief What a material gets to work with when writing one entity's
 * per-instance data. The transform and its model matrix are handed over
 * already fetched (the batching pass has both in hand, the matrix from the
 * TransformMatrixCache), so a material needs no component lookup for the
 * common case.
 */
class InstanceContext {
public:
  InstanceContext(ecs::World &worldRef, ecs::EntityID entityID,
                  const core::TransformComponent &transformRef,
                  const core::Mat4 &modelMatrix)
      : world(worldRef), entity(entityID), transform(transformRef),
        m_model(modelMatrix) {
  }

  ecs::World &world;
  ecs::EntityID entity;
  const core::TransformComponent &transform;

  /** Model matrix for this entity, as built from `transform`. */
  const core::Mat4 &modelMatrix() const {
    return m_model;
  }

private:
  const core::Mat4 &m_model;
};

/**
 * @brief Where one vec/float instance attribute lives inside an instance: a
 * name looked up once, so writing it per entity is a plain copy.
 *
 * Obtain one with findInstanceSlot() when a material receives its vertex
 * layout (IMaterial::resolveInstanceSlots) and keep it as a member. A slot
 * whose attribute the shader does not declare is invalid, and writing to an
 * invalid slot does nothing, exactly as writing an unknown name always has.
 */
struct InstanceSlot {
  static constexpr uint32_t kInvalid = UINT32_MAX;

  uint32_t offset = kInvalid; // in floats, from the start of one instance
  uint32_t components = 0;    // the attribute's width: 1 (float) to 4 (vec4)

  bool valid() const {
    return offset != kInvalid;
  }
};

/** The same for a mat4 attribute, which occupies four vec4 rows. */
struct InstanceMatrixSlot {
  uint32_t rowOffset[4] = {InstanceSlot::kInvalid, InstanceSlot::kInvalid,
                           InstanceSlot::kInvalid, InstanceSlot::kInvalid};

  bool valid() const {
    return rowOffset[0] != InstanceSlot::kInvalid &&
           rowOffset[1] != InstanceSlot::kInvalid &&
           rowOffset[2] != InstanceSlot::kInvalid &&
           rowOffset[3] != InstanceSlot::kInvalid;
  }
};

/** Looks `name` up in `layout` as a float/vec2/vec3/vec4 attribute. */
inline InstanceSlot findInstanceSlot(const api::VertexLayout &layout,
                                     core::Name name) {
  for (const api::VertexAttribute &attr : layout.attributes) {
    if (attr.name == name && attr.matrixRow < 0) {
      return {attr.offset / static_cast<uint32_t>(sizeof(float)),
              attr.componentCount};
    }
  }
  return {};
}

/** Looks `name` up in `layout` as a mat4 attribute. Invalid unless all four
 *  rows are present. */
inline InstanceMatrixSlot
findInstanceMatrixSlot(const api::VertexLayout &layout, core::Name name) {
  InstanceMatrixSlot slot;
  for (const api::VertexAttribute &attr : layout.attributes) {
    if (attr.name == name && attr.matrixRow >= 0 && attr.matrixRow < 4) {
      slot.rowOffset[attr.matrixRow] =
          attr.offset / static_cast<uint32_t>(sizeof(float));
    }
  }
  return slot;
}

/**
 * @brief Cursor over the floats of a single instance.
 *
 * Writes straight into the batch's flat instance buffer: no allocation, no
 * intermediate map. Two ways to address an attribute:
 *
 *  - By InstanceSlot / InstanceMatrixSlot, resolved once per material. This is
 *    the fast path and what a material's writeInstanceData() should use: the
 *    per-entity cost is a copy, with no search.
 *  - By Name, which searches the layout on every call. For one-off use.
 *
 * Either way, an attribute the shader does not declare is ignored (so a
 * material can offer more attributes than a given shader consumes), and a
 * value wider than its attribute is truncated to the attribute's component
 * count instead of spilling into the next one.
 *
 * Name construction interns a string; build Names once (a member or a static)
 * rather than from a literal on every call.
 */
class InstanceWriter {
public:
  InstanceWriter(const api::VertexLayout &layout, float *instance)
      : m_layout(layout), m_instance(instance) {
  }

  void set(core::Name name, float value) {
    setScalars(name, &value, 1);
  }
  void set(core::Name name, const core::Vec2 &value) {
    setScalars(name, core::value_ptr(value), 2);
  }
  void set(core::Name name, const core::Vec3 &value) {
    setScalars(name, core::value_ptr(value), 3);
  }
  void set(core::Name name, const core::Vec4 &value) {
    setScalars(name, core::value_ptr(value), 4);
  }

  /** A mat4 attribute occupies four consecutive vec4 slots sharing one name;
   *  slot r takes column r of the matrix. */
  void set(core::Name name, const core::Mat4 &value) {
    for (const api::VertexAttribute &attr : m_layout.attributes) {
      if (attr.name == name && attr.matrixRow >= 0) {
        std::memcpy(m_instance + attr.offset / sizeof(float),
                    core::value_ptr(value) + attr.matrixRow * 4,
                    sizeof(float) * 4);
      }
    }
  }

  void set(const InstanceSlot &slot, float value) {
    copyScalars<1>(slot, &value);
  }
  void set(const InstanceSlot &slot, const core::Vec2 &value) {
    copyScalars<2>(slot, core::value_ptr(value));
  }
  void set(const InstanceSlot &slot, const core::Vec3 &value) {
    copyScalars<3>(slot, core::value_ptr(value));
  }
  void set(const InstanceSlot &slot, const core::Vec4 &value) {
    copyScalars<4>(slot, core::value_ptr(value));
  }

  /** Row r of the attribute takes column r of the matrix. */
  void set(const InstanceMatrixSlot &slot, const core::Mat4 &value) {
    if (!slot.valid()) {
      return;
    }
    const float *src = core::value_ptr(value);
    for (int row = 0; row < 4; ++row) {
      std::memcpy(m_instance + slot.rowOffset[row], src + row * 4,
                  sizeof(float) * 4);
    }
  }

private:
  // `Count` is the width of the value being written, known at every call
  // site. In the common case the attribute is at least that wide, so the copy
  // is a fixed-size one the compiler turns into a few register moves; only a
  // value wider than its attribute takes the (runtime-length) truncating copy.
  template <uint32_t Count>
  void copyScalars(const InstanceSlot &slot, const float *src) {
    if (!slot.valid()) {
      return;
    }
    float *dst = m_instance + slot.offset;
    if (slot.components >= Count) {
      std::memcpy(dst, src, sizeof(float) * Count);
    } else {
      std::memcpy(dst, src, sizeof(float) * slot.components);
    }
  }

  void setScalars(core::Name name, const float *src, uint32_t count) {
    for (const api::VertexAttribute &attr : m_layout.attributes) {
      if (attr.name == name && attr.matrixRow < 0) {
        std::memcpy(m_instance + attr.offset / sizeof(float), src,
                    sizeof(float) * std::min(count, attr.componentCount));
        return;
      }
    }
  }

  const api::VertexLayout &m_layout;
  float *m_instance;
};

} // namespace componeng::renderer
