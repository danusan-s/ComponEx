#include "componeng/renderer/asset/mesh.hpp"
#include "gtest/gtest.h"

#include <string>

using namespace componeng::renderer;

namespace {

// Two triangles sharing an edge, as a plain OBJ: the loader makes a vertex per
// corner.
const char *kPlain = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\nvt 0 0\nvt "
                     "1 0\nvt 1 1\nvt 0 1\n"
                     "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n";

// The same mesh, marked as indexed: vertex i is entry i of v, vt and vn.
std::string indexedQuad() {
  return std::string(kIndexedMarker) +
         "\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
         "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
         "vn 0 0 1\nvn 0 0 1\nvn 0 0 1\nvn 0 0 1\n"
         "f 1/1/1 2/2/2 3/3/3\nf 1/1/1 3/3/3 4/4/4\n";
}

} // namespace

TEST(MeshLoader, PlainObjMakesAVertexPerTriangleCorner) {
  Mesh m;
  m.generateFromWavefrontObj(kPlain);
  EXPECT_EQ(m.m_vertices.size() / 8, 6u);
  EXPECT_EQ(m.m_indices.size(), 6u);
  EXPECT_EQ(m.m_indices[0], 0u);
  EXPECT_EQ(m.m_indices[5], 5u);
}

TEST(MeshLoader, IndexedObjKeepsSharedVerticesShared) {
  Mesh m;
  m.generateFromWavefrontObj(indexedQuad());
  EXPECT_EQ(m.m_vertices.size() / 8, 4u);
  ASSERT_EQ(m.m_indices.size(), 6u);
  const unsigned expected[] = {0, 1, 2, 0, 2, 3};
  for (int i = 0; i < 6; ++i)
    EXPECT_EQ(m.m_indices[i], expected[i]);
}

TEST(MeshLoader, IndexedVerticesAreUploadedInFileOrderWithTheirOwnAttributes) {
  Mesh m;
  m.generateFromWavefrontObj(indexedQuad());
  // Interleaved position(3) normal(3) uv(2); vertex 2 is (1,1,0), uv (1,1).
  const float *v2 = &m.m_vertices[2 * 8];
  EXPECT_FLOAT_EQ(v2[0], 1.0f);
  EXPECT_FLOAT_EQ(v2[1], 1.0f);
  EXPECT_FLOAT_EQ(v2[2], 0.0f);
  EXPECT_FLOAT_EQ(v2[5], 1.0f);
  EXPECT_FLOAT_EQ(v2[6], 1.0f);
  EXPECT_FLOAT_EQ(v2[7], 1.0f);
}

TEST(MeshLoader, IndexedAndPlainDrawTheSameTriangles) {
  Mesh plain, indexed;
  plain.generateFromWavefrontObj(kPlain);
  indexed.generateFromWavefrontObj(indexedQuad());
  ASSERT_EQ(plain.m_indices.size(), indexed.m_indices.size());
  for (size_t i = 0; i < plain.m_indices.size(); ++i) {
    for (int k = 0; k < 8; ++k) {
      EXPECT_FLOAT_EQ(plain.m_vertices[plain.m_indices[i] * 8 + k],
                      indexed.m_vertices[indexed.m_indices[i] * 8 + k])
          << "corner " << i << " component " << k;
    }
  }
}

TEST(MeshLoader, IndexedQuadsAreSplitIntoTwoTriangles) {
  Mesh m;
  m.generateFromWavefrontObj(std::string(kIndexedMarker) +
                             "\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                             "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                             "vn 0 0 1\nvn 0 0 1\nvn 0 0 1\nvn 0 0 1\n"
                             "f 1/1/1 2/2/2 3/3/3 4/4/4\n");
  ASSERT_EQ(m.m_indices.size(), 6u);
  const unsigned expected[] = {0, 1, 2, 0, 2, 3};
  for (int i = 0; i < 6; ++i)
    EXPECT_EQ(m.m_indices[i], expected[i]);
}

TEST(MeshLoader, IndexedFileWithMismatchedAttributeCountsIsRejected) {
  Mesh m;
  m.generateFromWavefrontObj(std::string(kIndexedMarker) +
                             "\nv 0 0 0\nv 1 0 0\nv 1 1 0\n"
                             "vt 0 0\nvt 1 0\nvn 0 0 1\nvn 0 0 1\nvn 0 0 1\n"
                             "f 1/1/1 2/2/2 3/3/3\n");
  EXPECT_TRUE(m.m_indices.empty());
}

TEST(MeshLoader, IndexedFaceNamingAMissingVertexAddsNothing) {
  Mesh m;
  m.generateFromWavefrontObj(
      std::string(kIndexedMarker) +
      "\nv 0 0 0\nv 1 0 0\nv 1 1 0\n"
      "vt 0 0\nvt 1 0\nvt 1 1\nvn 0 0 1\nvn 0 0 1\nvn 0 0 1\n"
      "f 1/1/1 2/2/2 9/9/9\n");
  EXPECT_TRUE(m.m_indices.empty());
}
