#include "componeng/renderer/asset/mesh.hpp"

#include "componeng/utils/logger.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace componeng::renderer {

void Mesh::uploadToGPU(api::IRenderDevice &device) {
  if (m_vertices.empty() || m_indices.empty()) {
    LOG_ERROR("Cannot initialize buffers for an empty mesh.");
    return;
  }

  computeBounds();
  m_impl = device.createMesh();
  m_impl->upload(m_vertices.data(), m_vertices.size() / 8, m_indices.data(),
                 m_indices.size(), api::defaultMeshLayout());
}

void Mesh::computeBounds() {
  // Vertices are interleaved position(3) normal(3) uv(2).
  constexpr size_t kFloatsPerVertex = 8;
  core::Vec3 lo(m_vertices[0], m_vertices[1], m_vertices[2]);
  core::Vec3 hi = lo;
  for (size_t i = 0; i + 2 < m_vertices.size(); i += kFloatsPerVertex) {
    lo.x = std::min(lo.x, m_vertices[i]);
    lo.y = std::min(lo.y, m_vertices[i + 1]);
    lo.z = std::min(lo.z, m_vertices[i + 2]);
    hi.x = std::max(hi.x, m_vertices[i]);
    hi.y = std::max(hi.y, m_vertices[i + 1]);
    hi.z = std::max(hi.z, m_vertices[i + 2]);
  }
  m_bounds.min = lo;
  m_bounds.max = hi;
}

void Mesh::generateFromWavefrontObj(const std::string &data) {
  LOG_INFO("Generating model");
  std::istringstream stream(data);
  std::string line;

  std::vector<std::array<float, 3>> positions;
  std::vector<std::array<float, 3>> normals;
  std::vector<std::array<float, 2>> texCoords;

  // Set by the kIndexedMarker line: v, vt and vn entry i together make vertex
  // i, and a face corner names a vertex by its v index alone (see below).
  bool indexed = false;

  while (std::getline(stream, line)) {
    std::istringstream lineStream(line);
    std::string prefix;
    lineStream >> prefix;

    if (prefix == kIndexedMarker) {
      indexed = true;
    } else if (prefix == "v") {
      float x, y, z;
      lineStream >> x >> y >> z;
      positions.push_back({x, y, z});
    } else if (prefix == "vn") {
      float nx, ny, nz;
      lineStream >> nx >> ny >> nz;
      normals.push_back({nx, ny, nz});
    } else if (prefix == "vt") {
      float u, v;
      lineStream >> u >> v;
      texCoords.push_back({u, v});
    } else if (prefix == "f" && indexed) {
      // Indexed mesh: the file already shares vertices, so a corner is just a
      // vertex number (the first of its v/vt/vn indices) and the face becomes
      // triangles over those vertices, with no vertex made per corner.
      std::vector<unsigned int> corners;
      std::string vertexStr;
      while (lineStream >> vertexStr) {
        const int index = std::atoi(vertexStr.c_str()) - 1;
        if (index < 0 || index >= static_cast<int>(positions.size())) {
          LOG_ERROR("Invalid vertex index in indexed OBJ file: %s.",
                    vertexStr.c_str());
          corners.clear();
          break;
        }
        corners.push_back(static_cast<unsigned int>(index));
      }
      for (size_t k = 1; k + 1 < corners.size(); ++k) {
        m_indices.push_back(corners[0]);
        m_indices.push_back(corners[k]);
        m_indices.push_back(corners[k + 1]);
      }
    } else if (prefix == "f") {
      std::string vertexStr;
      int faceVertCount = 0;
      while (lineStream >> vertexStr) {
        std::istringstream vertexStream(vertexStr);
        std::string posIndexStr, texIndexStr, normIndexStr;

        std::getline(vertexStream, posIndexStr, '/');
        std::getline(vertexStream, texIndexStr, '/');
        std::getline(vertexStream, normIndexStr, '/');

        if (posIndexStr.empty() || texIndexStr.empty() ||
            normIndexStr.empty()) {
          LOG_ERROR("Invalid vertex format in OBJ file: %s.",
                    vertexStr.c_str());
          continue;
        }

        int posIndex = std::stoi(posIndexStr) - 1;
        int texIndex = std::stoi(texIndexStr) - 1;
        int normIndex = std::stoi(normIndexStr) - 1;

        if (posIndex < 0 || posIndex >= static_cast<int>(positions.size())) {
          LOG_ERROR("Invalid position index in OBJ file: %s.",
                    posIndexStr.c_str());
          continue;
        }
        if (texIndex < 0 || texIndex >= static_cast<int>(texCoords.size())) {
          LOG_ERROR("Invalid texture coordinate index in OBJ file: %s.",
                    texIndexStr.c_str());
          continue;
        }
        if (normIndex < 0 || normIndex >= static_cast<int>(normals.size())) {
          LOG_ERROR("Invalid normal index in OBJ file: %s.",
                    normIndexStr.c_str());
          continue;
        }

        const auto &pos = positions[posIndex];
        const auto &norm = normals[normIndex];
        const auto &tex = texCoords[texIndex];

        m_vertices.push_back(pos[0]);
        m_vertices.push_back(pos[1]);
        m_vertices.push_back(pos[2]);
        m_vertices.push_back(norm[0]);
        m_vertices.push_back(norm[1]);
        m_vertices.push_back(norm[2]);
        m_vertices.push_back(tex[0]);
        m_vertices.push_back(tex[1]);

        ++faceVertCount;
      }
      if (faceVertCount == 3) {
        m_indices.push_back(static_cast<unsigned int>(m_vertices.size() / 8) -
                            3);
        m_indices.push_back(static_cast<unsigned int>(m_vertices.size() / 8) -
                            2);
        m_indices.push_back(static_cast<unsigned int>(m_vertices.size() / 8) -
                            1);
      } else if (faceVertCount == 4) {
        m_indices.push_back(static_cast<unsigned int>(m_vertices.size() / 8) -
                            4);
        m_indices.push_back(static_cast<unsigned int>(m_vertices.size() / 8) -
                            3);
        m_indices.push_back(static_cast<unsigned int>(m_vertices.size() / 8) -
                            2);

        m_indices.push_back(static_cast<unsigned int>(m_vertices.size() / 8) -
                            4);
        m_indices.push_back(static_cast<unsigned int>(m_vertices.size() / 8) -
                            2);
        m_indices.push_back(static_cast<unsigned int>(m_vertices.size() / 8) -
                            1);
      }
    }
  }

  if (indexed) {
    if (normals.size() != positions.size() ||
        texCoords.size() != positions.size()) {
      LOG_ERROR("Indexed OBJ file has %zu positions but %zu normals and %zu "
                "texture coordinates; they must match.",
                positions.size(), normals.size(), texCoords.size());
      m_indices.clear();
      return;
    }
    m_vertices.reserve(positions.size() * 8);
    for (size_t i = 0; i < positions.size(); ++i) {
      m_vertices.insert(m_vertices.end(), positions[i].begin(),
                        positions[i].end());
      m_vertices.insert(m_vertices.end(), normals[i].begin(), normals[i].end());
      m_vertices.insert(m_vertices.end(), texCoords[i].begin(),
                        texCoords[i].end());
    }
  }
}

size_t Mesh::indexCount() const {
  return m_impl->indexCount();
}

} // namespace componeng::renderer
