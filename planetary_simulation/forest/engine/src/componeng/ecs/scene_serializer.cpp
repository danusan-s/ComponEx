#include "componeng/ecs/scene_serializer.hpp"

#include "componeng/camera/main_camera.hpp"
#include "componeng/ecs/world.hpp"
#include "componeng/utils/logger.hpp"

#include <fstream>

namespace componeng::ecs {

bool SceneSerializer::save(World &world, const std::string &filename) {
  std::array<Archetype, MAX_ARCHETYPES> &archetypes =
      world.m_archetypeManager.getArchetypes();

  nlohmann::json sceneJson;
  sceneJson["entities"] = nlohmann::json::array();

  for (Archetype &archetype : archetypes) {
    if (archetype.getEntityCount() == 0)
      continue;

    const Signature &signature = archetype.m_signature;

    // For each entity populate a JSON object with its components
    for (size_t i = 0; i < archetype.getEntityCount(); ++i) {
      EntityID entity = archetype.getEntityForRow(i);
      nlohmann::json entityJson;

      for (ComponentID c = 0; c < MAX_COMPONENTS; ++c) {
        if (!signature.test(c))
          continue;

        const ComponentInfo &info =
            world.m_componentRegistry.getComponentInfo(c);
        if (!info.serializer) {
          LOG_ERROR(
              "No serializer for component ID %d (%s), skipping serialization",
              c, info.name);
          continue;
        }

        const void *componentPtr = archetype.readColumn(c).at(i);
        entityJson[info.name] = info.serializer(componentPtr);
      }

      if (world.hasResource<camera::MainCamera>() &&
          entity == world.getResource<camera::MainCamera>().entity) {
        entityJson["_isMainCamera"] = true;
      }

      sceneJson["entities"].push_back(entityJson);
    }
  }

  std::ofstream file(filename);
  if (!file.is_open()) {
    LOG_ERROR("Failed to open file %s for saving scene", filename.c_str());
    return false;
  }

  file << sceneJson.dump(4);
  file.close();
  LOG_INFO("Scene saved to %s", filename.c_str());
  return true;
}

bool SceneSerializer::load(World &world, const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    LOG_ERROR("Failed to open file %s for loading scene", filename.c_str());
    return false;
  }

  nlohmann::json sceneJson;
  try {
    file >> sceneJson;
  } catch (const nlohmann::json::parse_error &e) {
    LOG_ERROR("Failed to parse scene %s: %s", filename.c_str(), e.what());
    return false;
  }
  file.close();

  if (!sceneJson.contains("entities") || !sceneJson["entities"].is_array()) {
    LOG_ERROR("Invalid scene format: missing 'entities' array");
    return false;
  }

  EntityID mainCameraEntity = INVALID_ENTITY;

  for (const auto &entityJson : sceneJson["entities"]) {
    EntityID entity = world.createEntity();
    LOG_INFO("Created entity %llu from scene",
             static_cast<unsigned long long>(entity));

    for (auto it = entityJson.begin(); it != entityJson.end(); ++it) {
      const std::string &componentName = it.key();
      const nlohmann::json &componentData = it.value();
      LOG_INFO("Adding component %s to entity", componentName.c_str());

      if (componentName == "_isMainCamera") {
        continue;
      }

      // Find component ID by name using O(1) lookup
      ComponentID componentID;
      try {
        componentID =
            world.m_componentRegistry.getComponentIDByName(componentName);
      } catch (const std::runtime_error &) {
        LOG_ERROR("Unknown component type '%s' in scene, skipping",
                  componentName.c_str());
        continue;
      }

      const ComponentInfo &info =
          world.m_componentRegistry.getComponentInfo(componentID);
      if (!info.deserializer) {
        LOG_ERROR("No deserializer for component '%s' (ID %d), skipping",
                  componentName.c_str(), componentID);
        continue;
      }

      // Scene data is untrusted; a malformed field must skip its component
      // rather than abort the whole load.
      void *componentPtr = nullptr;
      try {
        componentPtr = info.deserializer(componentData);
      } catch (const std::exception &e) {
        LOG_ERROR("Failed to deserialize component '%s': %s, skipping",
                  componentName.c_str(), e.what());
        continue;
      }

      // addComponentById memcpys the bytes into archetype storage, so the
      // heap object the deserializer handed back must be released here --
      // including if that call throws.
      try {
        world.addComponentById(entity, componentID, componentPtr);
      } catch (...) {
        if (info.deleter) {
          info.deleter(componentPtr);
        }
        throw;
      }
      if (info.deleter) {
        info.deleter(componentPtr);
      }
    }

    if (entityJson.contains("_isMainCamera") &&
        entityJson["_isMainCamera"].get<bool>()) {
      mainCameraEntity = entity;
    }
  }

  if (mainCameraEntity != INVALID_ENTITY) {
    world.setResource(camera::MainCamera{mainCameraEntity});
    LOG_INFO("Main camera set to entity %llu",
             static_cast<unsigned long long>(mainCameraEntity));
  }

  LOG_INFO("Scene loaded from %s", filename.c_str());
  return true;
}

} // namespace componeng::ecs
