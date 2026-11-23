#pragma once

#include "componeng/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace componeng::renderer {

struct DrawKey {
  core::HandleID meshID = 0;
  core::HandleID materialID = 0;

  bool operator==(const DrawKey &other) const {
    return materialID == other.materialID && meshID == other.meshID;
  }
};

// hash combine algo so it works on both 32 and 64 bit platforms
struct DrawKeyHash {
  std::size_t operator()(const DrawKey &k) const {
    std::size_t h = std::hash<uint32_t>{}(k.meshID);
    h ^= std::hash<uint32_t>{}(k.materialID) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

} // namespace componeng::renderer
