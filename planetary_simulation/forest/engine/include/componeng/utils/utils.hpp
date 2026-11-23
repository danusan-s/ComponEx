#pragma once

#include <string>

namespace componeng::utils {

/**
 * @brief Static utility functions for filesystem path resolution.
 *
 * Provides helpers to locate the executable directory and construct
 * absolute paths to assets relative to the project root.
 */
class Utils {
public:
  static std::string getExecutablePath();
  static std::string getExecutableDir();
  /** Resolves `relativePath` (e.g. "assets/shaders/foo.vert") to an absolute
   *  path. Roots added with addAssetRoot() are searched first, newest first,
   *  then the engine's own source root; the first one containing the file
   *  wins. If none does, the engine-root path is returned so the caller's
   *  "file not found" message still names a sensible location. */
  static std::string getAssetPath(const std::string &relativePath);

  /** Lets a game keep its assets outside the engine tree: `root` is searched
   *  by getAssetPath before the engine's own root. */
  static void addAssetRoot(const std::string &root);
};

} // namespace componeng::utils
