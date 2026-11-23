#include "componeng/utils/utils.hpp"

#include <filesystem>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

namespace componeng::utils {

std::string Utils::getExecutablePath() {
#if defined(_WIN32)
  char buffer[MAX_PATH];
  GetModuleFileNameA(NULL, buffer, MAX_PATH);
  return std::string(buffer);
#elif defined(__APPLE__)
  char buffer[1024];
  uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) == 0) {
    return std::string(buffer);
  }
  return "";
#elif defined(__linux__)
  char buffer[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
  if (count != -1) {
    return std::string(buffer, count);
  }
  return "";
#else
  return "";
#endif
}

std::string Utils::getExecutableDir() {
  std::filesystem::path exePath = getExecutablePath();
  if (exePath.empty()) {
    return std::filesystem::current_path().string();
  }
  return exePath.parent_path().string();
}

static std::vector<std::filesystem::path> &extraAssetRoots() {
  static std::vector<std::filesystem::path> roots;
  return roots;
}

void Utils::addAssetRoot(const std::string &root) {
  extraAssetRoots().push_back(root);
}

std::string Utils::getAssetPath(const std::string &relativePath) {
  static std::filesystem::path root = [] {
    // This file is at <root>/src/componeng/utils/utils.cpp, so four hops up
    // lands on the project root. Move this file and you must update the count.
    constexpr int HOPS_TO_PROJECT_ROOT = 4;
    std::filesystem::path p = __FILE__;
    for (int i = 0; i < HOPS_TO_PROJECT_ROOT; ++i) {
      p = p.parent_path();
    }
    return p;
  }();

  const auto &extra = extraAssetRoots();
  for (auto it = extra.rbegin(); it != extra.rend(); ++it) {
    std::filesystem::path candidate = *it / relativePath;
    if (std::filesystem::exists(candidate)) {
      return std::filesystem::weakly_canonical(candidate).string();
    }
  }

  // A packaged build has assets/ next to the executable.
  std::filesystem::path beside =
      std::filesystem::path(getExecutableDir()) / relativePath;
  if (std::filesystem::exists(beside)) {
    return std::filesystem::weakly_canonical(beside).string();
  }

  std::filesystem::path assetPath = root / relativePath;

  return std::filesystem::weakly_canonical(assetPath).string();
}

} // namespace componeng::utils
