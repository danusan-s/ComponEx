#pragma once

#include "componeng/ecs/archetype/archetype.hpp"
#include "componeng/ecs/entity.hpp"

#include <stdexcept>

namespace componeng::ecs {

/**
 * @brief Creates, stores, and looks up archetypes by their component Signature.
 *
 * Each unique Signature maps to exactly one Archetype. When a new signature
 * is encountered a fresh archetype is created and cached. This manager is
 * the sole owner of all archetypes in the World.
 */
class ArchetypeManager {
private:
  std::array<Archetype, MAX_ARCHETYPES> m_archetypes;
  std::unordered_map<Signature, ArchetypeID> m_signatureToArchetypeID;
  size_t m_archetypeCount = 0;

public:
  /**
   * @return the id of the archetype for this signature, creating it if this
   * signature has not been seen before. Callers keep the id rather than the
   * reference, so it stays valid as archetypes are added.
   */
  ArchetypeID getOrCreate(const Signature &signature,
                          ComponentRegistry &componentRegistry) {
    if (signature == Signature(0)) {
      throw std::runtime_error("ArchetypeManager: signature cannot be empty");
    }

    auto it = m_signatureToArchetypeID.find(signature);
    if (it != m_signatureToArchetypeID.end()) {
      return it->second;
    }

    if (m_archetypeCount >= MAX_ARCHETYPES) {
      throw std::runtime_error("ArchetypeManager: MAX_ARCHETYPES exceeded");
    }

    ArchetypeID newID = static_cast<ArchetypeID>(m_archetypeCount++);
    m_archetypes[newID].init(signature, componentRegistry);
    m_signatureToArchetypeID[signature] = newID;
    return newID;
  }

  Archetype &getByID(ArchetypeID id) {
    if (id >= m_archetypeCount) {
      throw std::runtime_error("ArchetypeManager: Invalid ArchetypeID");
    }
    return m_archetypes[id];
  }

  std::array<Archetype, MAX_ARCHETYPES> &getArchetypes() {
    return m_archetypes;
  }

  /** Live archetypes occupy [0, count); the rest of the array is unused. */
  std::size_t count() const {
    return m_archetypeCount;
  }
};

} // namespace componeng::ecs
