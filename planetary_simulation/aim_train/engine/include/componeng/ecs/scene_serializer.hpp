#pragma once

#include <string>

namespace componeng::ecs {

class World;

/**
 * @brief Persists/restores a World's entities and components as JSON.
 *
 * Walks every archetype's live entities, serializing each component via its
 * registered ComponentInfo::serializer/deserializer (see ComponentRegistry),
 * and separately tracks which entity (if any) is the scene's main camera.
 */
class SceneSerializer {
public:
  /// @return true on success; false if the file could not be written.
  static bool save(World &world, const std::string &filename);

  /**
   * Adds the file's entities to @p world — it does NOT clear the existing
   * scene first. Callers wanting replace-semantics must destroy the old
   * entities themselves.
   * @return true on success; false if the file was missing or malformed.
   */
  static bool load(World &world, const std::string &filename);
};

} // namespace componeng::ecs
