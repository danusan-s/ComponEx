#pragma once

#include "componeng/core/types.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/culling/bounds.hpp"

#include <memory>
#include <string>
#include <vector>

namespace componeng::renderer {

/**
 * First token of the line that marks an OBJ file as pre-indexed (a comment to
 * any other OBJ reader). By default the loader makes one vertex per triangle
 * corner, whatever the file shares. In a marked file entry i of `v`, `vt` and
 * `vn` together are vertex i, and a face lists vertex numbers (`f 1/1/1 2/2/2
 * 3/3/3`; the first number of each corner is the one used), so shared vertices
 * stay shared and the order the file puts them in, which an offline tool can
 * choose for the GPU's vertex cache, is the order they are uploaded in.
 * tools/optimize_meshes.py writes such files.
 */
inline constexpr const char *kIndexedMarker = "#componeng_indexed";

/**
 * @brief Mesh resource with vertex/index buffers.
 *
 * Parses Wavefront OBJ data and uploads to the GPU via the IMesh interface.
 * The underlying GPU resource is owned by this class.
 */
class Mesh {
public:
  Mesh() = default;

  /** Uploads the parsed data through `device`, which owns how the GPU
   *  resource is created. */
  void uploadToGPU(api::IRenderDevice &device);

  // Helper to generate mesh data from Wavefront OBJ format (vertices, normals,
  // texcoords). Files carrying kIndexedMarker are loaded as indexed meshes.
  void generateFromWavefrontObj(const std::string &data);

  // Access to the underlying IMesh for render device operations
  api::IMesh &getImpl() {
    return *m_impl;
  }
  const api::IMesh &getImpl() const {
    return *m_impl;
  }

  size_t indexCount() const;

  /** Local-space bounds of the uploaded vertices; used for frustum culling. */
  const Aabb &bounds() const {
    return m_bounds;
  }

  // Raw vertex/index data (used during parsing before GPU upload)
  std::vector<float> m_vertices;
  std::vector<unsigned int> m_indices;

private:
  void computeBounds();

  std::unique_ptr<api::IMesh> m_impl;
  Aabb m_bounds;
};

} // namespace componeng::renderer
