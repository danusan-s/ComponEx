#pragma once

#include "componeng/ecs/archetype/component_column.hpp"
#include "componeng/ecs/component_registry.hpp"
#include "componeng/ecs/entity.hpp"

#include <stdexcept>
#include <vector>

namespace componeng::ecs {

/**
 * @brief An archetype groups all entities that share the exact same set of
 * component types.
 *
 * Each archetype holds one ComponentColumn per component type, so iterating
 * over entities touches contiguous memory for each component — this is the
 * core cache-friendly data layout of the ECS.
 *
 */
class Archetype {
private:
  std::vector<ComponentColumn> m_columns;
  std::vector<EntityID> m_rowToEntity;

  /**
   * Column index of a component: how many components with a lower id this
   * archetype holds. Costs a popcount instead of a MAX_COMPONENTS-sized map.
   */
  std::size_t columnOf(ComponentID c) const {
    static_assert(MAX_COMPONENTS <= 64,
                  "columnOf builds its mask from a 64-bit word; going wider "
                  "needs a Signature type that exposes its words");
    const Signature below(c == 0 ? 0ull : (~0ull >> (64 - c)));
    return (m_signature & below).count();
  }

public:
  Signature m_signature;

  void init(Signature signature, ComponentRegistry &registry) {
    this->m_signature = signature;

    // Ascending order matters: columnOf() depends on it.
    for (size_t i = 0; i < MAX_COMPONENTS; ++i) {
      if (signature.test(i)) {
        ComponentInfo &info = registry.getComponentInfo(i);
        m_columns.emplace_back(info.size, info.alignment, info.destructor);
      }
    }
  }

  /**
   * @return the row the entity was placed in.
   * caller responsible for copying data into the row
   */
  std::size_t addEntity(EntityID entity) {
    const std::size_t row = m_rowToEntity.size();
    for (auto &col : m_columns) {
      col.pushBackEmpty();
    }
    m_rowToEntity.push_back(entity);
    return row;
  }

  /**
   * Swap-removes the given row from every component column.
   *
   * Takes a row rather than an EntityID because the caller already holds the
   * row in the entity's record; looking it up again would mean keeping a
   * second copy of that mapping here.
   *
   * @return the EntityID swapped into the freed row, or INVALID_ENTITY if the
   * removed row was the last one. The caller must repoint that entity's
   * record, otherwise every later access reads the wrong row.
   */
  EntityID removeEntityAtRow(std::size_t row) {
    if (row >= m_rowToEntity.size()) {
      throw std::runtime_error("Archetype: Invalid row index");
    }
    const std::size_t lastRow = m_rowToEntity.size() - 1;

    for (auto &col : m_columns) {
      col.remove(row);
    }

    if (row == lastRow) {
      m_rowToEntity.pop_back();
      return INVALID_ENTITY;
    }

    const EntityID moved = m_rowToEntity[lastRow];
    m_rowToEntity[row] = moved;
    m_rowToEntity.pop_back();
    return moved;
  }

  /**
   * Read-only access to a component's column. Does not touch its version, so
   * it is the right call for anything that only looks at the data.
   */
  const ComponentColumn &readColumn(ComponentID c) const {
    if (!m_signature.test(c)) {
      throw std::runtime_error("Archetype: Component not in archetype");
    }
    return m_columns[columnOf(c)];
  }

  /**
   * Mutable access to a component's column. Bumps the column's version, so
   * call it when the caller is about to write (or hand out writers), not as a
   * general-purpose accessor.
   */
  ComponentColumn &writeColumn(ComponentID c) {
    if (!m_signature.test(c)) {
      throw std::runtime_error("Archetype: Component not in archetype");
    }
    ComponentColumn &column = m_columns[columnOf(c)];
    column.markWritten();
    return column;
  }

  /** Current version of a component's column. */
  ColumnVersion columnVersion(ComponentID c) const {
    return readColumn(c).version();
  }

  template <typename T> const T &read(ComponentID c, std::size_t row) const {
    return readColumn(c).get<T>(row);
  }

  template <typename T> T &write(ComponentID c, std::size_t row) {
    return writeColumn(c).get<T>(row);
  }

  EntityID getEntityForRow(std::size_t row) const {
    if (row >= m_rowToEntity.size()) {
      throw std::runtime_error("Archetype: Row index out of bounds");
    }
    return m_rowToEntity[row];
  }

  // Tracks rows, not columns, so this stays correct for an archetype whose
  // signature is empty (entities with no components still occupy rows).
  std::size_t getEntityCount() const {
    return m_rowToEntity.size();
  }
};

} // namespace componeng::ecs
