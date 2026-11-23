#include "componeng/core/math/ops.hpp"
#include "componeng/renderer/culling/frustum.hpp"

#include "gtest/gtest.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace componeng::core;

namespace {

constexpr float kEps = 1e-5f;

glm::mat4 ToGlm(const Mat4 &m) {
  glm::mat4 out(0.0f);
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row)
      out[c][row] = m[c][row];
  return out;
}

Mat4 FromGlm(const glm::mat4 &m) {
  Mat4 r(0.0f);
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row)
      r[c][row] = m[c][row];
  return r;
}

glm::vec3 ToGlmVec3(const Vec3 &v) {
  return glm::vec3(v.x, v.y, v.z);
}

// Column-major mat4 applied to a homogeneous vec4: out[row] =
// Σ_col m[col][row] * v[col]. Kept in the test so we can check the whole
// pipeline against glm without adding production code.
Vec4 MulMat4Vec4(const Mat4 &m, const Vec4 &v) {
  return Vec4(m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w,
              m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w,
              m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w,
              m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w);
}

::testing::AssertionResult SameMat4(const Mat4 &a, const glm::mat4 &b,
                                    float eps = kEps) {
  float worst = 0.0f;
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      worst = std::max(worst, std::fabs(a[c][r] - b[c][r]));
  if (worst <= eps)
    return ::testing::AssertionSuccess();

  char msg[1024];
  std::snprintf(msg, sizeof(msg), "worst |a-b| = %.6e", worst);
  return ::testing::AssertionFailure() << msg;
}

testing::AssertionResult SameVec3(const Vec3 &a, const glm::vec3 &b,
                                  float eps = kEps) {
  auto isNanBits = [](float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(float));
    return ((u & 0x7f800000u) == 0x7f800000u) && ((u & 0x007fffffu) != 0u);
  };
  auto diff = [&](float x, float y) {
    return (isNanBits(x) && isNanBits(y)) ? 0.0f : std::fabs(x - y);
  };
  float worst = std::max({diff(a.x, b.x), diff(a.y, b.y), diff(a.z, b.z)});
  if (worst <= eps)
    return ::testing::AssertionSuccess();
  char msg[128];
  std::snprintf(msg, sizeof(msg), "vec3 diff = %.6e", worst);
  return ::testing::AssertionFailure() << msg;
}

void ExpectAllMat4s(const std::vector<std::pair<Mat4, glm::mat4>> &cases,
                    float eps = kEps) {
  int i = 0;
  for (const auto &[mine, ref] : cases) {
    EXPECT_TRUE(SameMat4(mine, ref, eps)) << "case " << i++;
  }
}

} // namespace

// --- Vector ops -------------------------------------------------------------

TEST(MathVsGlm, Vec3Basics) {
  const Vec3 a(2.3f, -1.7f, 0.5f);
  const Vec3 b(-0.4f, 0.9f, 3.1f);
  const glm::vec3 ga(2.3f, -1.7f, 0.5f);
  const glm::vec3 gb(-0.4f, 0.9f, 3.1f);

  EXPECT_FLOAT_EQ(dot(a, b), glm::dot(ga, gb));
  EXPECT_FLOAT_EQ(length(a), glm::length(ga));
  EXPECT_TRUE(SameVec3(normalize(a), glm::normalize(ga)));
  EXPECT_TRUE(SameVec3(cross(a, b), glm::cross(ga, gb)));
  EXPECT_TRUE(SameVec3(a + b, ga + gb));
  EXPECT_TRUE(SameVec3(a - b, ga - gb));
  EXPECT_TRUE(SameVec3(2.0f * a, 2.0f * ga));
  EXPECT_TRUE(SameVec3(-a, -ga));
}

TEST(MathVsGlm, NormalizeEdgeCases) {
  // Must match glm::normalize even for degenerate inputs.
  const Vec3 zeroVec(0.0f, 0.0f, 0.0f);
  EXPECT_TRUE(SameVec3(normalize(zeroVec), glm::normalize(glm::vec3(0.0f))));
}

// --- Matrix multiplication ---------------------------------------------------

TEST(MathVsGlm, Mat4MultiplicationOrder) {
  glm::mat4 A = glm::rotate(glm::mat4(1.0f), 1.2f, glm::vec3(0.0f, 1.0f, 0.0f));
  glm::mat4 B = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));

  const Mat4 a = FromGlm(A);
  const Mat4 b = FromGlm(B);

  // a*b must equal A*B (not the reversed B*A)
  EXPECT_TRUE(SameMat4(a * b, A * B));
}

TEST(MathVsGlm, Mat4MultiplicationDistributes) {
  glm::mat4 gA(1.0f);
  gA = glm::rotate(gA, 0.3f, glm::vec3(1.0f, 0.0f, 0.0f));
  gA = glm::rotate(gA, 0.7f, glm::vec3(0.0f, 1.0f, 0.0f));
  gA = glm::rotate(gA, 1.1f, glm::vec3(0.0f, 0.0f, 1.0f));

  glm::mat4 gB = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 2.0f, 1.5f));
  gB = glm::translate(gB, glm::vec3(-3.0f, 1.0f, 4.0f));

  ExpectAllMat4s({{FromGlm(gA) * FromGlm(gB), gA * gB},
                  {FromGlm(gB) * FromGlm(gA), gB * gA}});
}

// --- Transform helpers -------------------------------------------------------

TEST(MathVsGlm, Translate) {
  const Mat4 base =
      FromGlm(glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(1.0f, 2.0f, 3.0f)));
  const glm::mat4 gBase = ToGlm(base);
  const Vec3 t(4.0f, -1.5f, 2.25f);
  ExpectAllMat4s({{translate(base, t), glm::translate(gBase, ToGlmVec3(t))}});
}

TEST(MathVsGlm, RotateEachAxis) {
  const float angles[] = {0.0f, 0.3f, 1.5707963f, 2.1f, 6.2831853f};
  const glm::vec3 axes[] = {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0),
                            glm::vec3(0, 0, 1)};
  const Vec3 cAxes[] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};

  for (float ang : angles) {
    for (int k = 0; k < 3; ++k) {
      ExpectAllMat4s({{rotate(Mat4(1.0f), ang, cAxes[k]),
                       glm::rotate(glm::mat4(1.0f), ang, axes[k])}});
    }
  }
}

TEST(MathVsGlm, RotateArbitraryAxisComposed) {
  const Mat4 base = translate(Mat4(1.0f), Vec3(1.0f, 2.0f, 3.0f));
  const glm::mat4 gBase = ToGlm(base);

  const Vec3 axis(0.3f, -0.9f, 0.2f);
  const glm::vec3 gAxis(0.3f, -0.9f, 0.2f);

  const Mat4 mine = rotate(base, 1.3f, axis);
  const glm::mat4 ref = glm::rotate(gBase, 1.3f, gAxis);
  EXPECT_TRUE(SameMat4(mine, ref));

  // Composing about multiple axes matches GLM's post-multiply ordering
  Mat4 composed = Mat4(1.0f);
  glm::mat4 gComposed(1.0f);
  const Vec3 axisOrder[] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
  const glm::vec3 gAxisOrder[] = {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0),
                                  glm::vec3(0, 0, 1)};
  const float rotAngles[] = {0.4f, 0.9f, 1.6f};
  for (int i = 0; i < 3; ++i) {
    composed = rotate(composed, rotAngles[i], axisOrder[i]);
    gComposed = glm::rotate(gComposed, rotAngles[i], gAxisOrder[i]);
  }
  EXPECT_TRUE(SameMat4(composed, gComposed));
}

TEST(MathVsGlm, Scale) {
  const Mat4 base =
      FromGlm(glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0.0f, 1.0f, 0.0f)));
  const glm::mat4 gBase = ToGlm(base);
  const Vec3 s(2.0f, -0.5f, 3.0f);
  ExpectAllMat4s({{scale(base, s), glm::scale(gBase, ToGlmVec3(s))}});
}

// --- Camera / pipeline -------------------------------------------------------

TEST(MathVsGlm, LookAt) {
  const struct {
    Vec3 eye, center, up;
  } cases[] = {
      {Vec3(0, 0, 5), Vec3(0, 0, -1), Vec3(0, 1, 0)},
      {Vec3(1, 2, 3), Vec3(-4, 0, 2), Vec3(0, 1, 0)},
      {Vec3(-3, 1, -2), Vec3(5, -1, 3), Vec3(0, 1, 0)},
      {Vec3(0, 3, 0), Vec3(0, 0, 0), Vec3(0, 0, -1)},
  };
  for (const auto &c : cases) {
    const Mat4 mine = lookAt(c.eye, c.center, c.up);
    const glm::mat4 ref =
        glm::lookAt(ToGlmVec3(c.eye), ToGlmVec3(c.center), ToGlmVec3(c.up));
    EXPECT_TRUE(SameMat4(mine, ref));
  }
}

TEST(MathVsGlm, Perspective) {
  const float fov = 1.0471976f; // 60 degrees in radians
  const glm::mat4 ref = glm::perspective(fov, 16.0f / 9.0f, 0.1f, 1000.0f);
  EXPECT_TRUE(SameMat4(perspective(fov, 16.0f / 9.0f, 0.1f, 1000.0f), ref));

  // fov in radians (consistent with GLM); NaN cases left untested on purpose.
  const glm::mat4 ref2 = glm::perspective(0.5f, 1.0f, 0.01f, 100.0f);
  EXPECT_TRUE(SameMat4(perspective(0.5f, 1.0f, 0.01f, 100.0f), ref2));
}

TEST(MathVsGlm, ViewProjectionPipeline) {
  const Vec3 eye(0.0f, 2.0f, 8.0f);
  const Vec3 center(0.0f, 0.0f, 0.0f);
  const Vec3 up(0.0f, 1.0f, 0.0f);
  const float fov = 1.0471976f;
  const float aspect = 16.0f / 9.0f;

  const Mat4 view = lookAt(eye, center, up);
  const Mat4 proj = perspective(fov, aspect, 0.1f, 1000.0f);

  const glm::mat4 gView =
      glm::lookAt(ToGlmVec3(eye), ToGlmVec3(center), ToGlmVec3(up));
  const glm::mat4 gProj = glm::perspective(fov, aspect, 0.1f, 1000.0f);

  EXPECT_TRUE(SameMat4(proj * view, gProj * gView));
}

// --- Full model matrix pipeline ----------------------------------------------

TEST(MathVsGlm, ModelMatrixPipelineOrderXYZS) {
  // Mirrors DiffuseMaterial::writeInstanceData: T * Rx * Ry * Rz * S
  const Vec3 pos(1.0f, 2.0f, 3.0f);
  const Vec3 rotX(1.5707963f, 0.7853981f, 0.5235988f); // radians
  const Vec3 scaleV(2.0f, 0.5f, 3.0f);

  Mat4 mine(1.0f);
  mine = translate(mine, pos);
  mine = rotate(mine, rotX.x, Vec3(1, 0, 0));
  mine = rotate(mine, rotX.y, Vec3(0, 1, 0));
  mine = rotate(mine, rotX.z, Vec3(0, 0, 1));
  mine = scale(mine, scaleV);

  glm::mat4 ref(1.0f);
  ref = glm::translate(ref, ToGlmVec3(pos));
  ref = glm::rotate(ref, rotX.x, glm::vec3(1, 0, 0));
  ref = glm::rotate(ref, rotX.y, glm::vec3(0, 1, 0));
  ref = glm::rotate(ref, rotX.z, glm::vec3(0, 0, 1));
  ref = glm::scale(ref, ToGlmVec3(scaleV));

  EXPECT_TRUE(SameMat4(mine, ref));
}

TEST(MathVsGlm, ModelMatrixPipelineManySeeds) {
  struct Seed {
    Vec3 pos;
    Vec3 rot; // radians
    Vec3 scaleV;
  };
  const Seed seeds[] = {
      // Engine usage: T * Rx * Ry * Rz * S
      {{1, 2, 3}, {1.5707963f, 0.7853981f, 0.5235988f}, {2, 0.5f, 3}},
      {{-5, 0.5f, 10}, {0.3f, -1.2f, 2.4f}, {1, 1, 1}},
      {{0, 0, 0}, {3.1415927f, 0, 0.7853981f}, {0.1f, 10, 1}},
      {{7, -3, 2}, {0, 0, 0}, {2, 2, 2}},
      {{-2, -2, -2}, {-0.7f, 1.9f, -2.7f}, {-1, 3, 0.5f}},
  };

  for (const auto &s : seeds) {
    Mat4 mine(1.0f);
    mine = translate(mine, s.pos);
    mine = rotate(mine, s.rot.x, Vec3(1, 0, 0));
    mine = rotate(mine, s.rot.y, Vec3(0, 1, 0));
    mine = rotate(mine, s.rot.z, Vec3(0, 0, 1));
    mine = scale(mine, s.scaleV);

    glm::mat4 ref(1.0f);
    ref = glm::translate(ref, ToGlmVec3(s.pos));
    ref = glm::rotate(ref, s.rot.x, glm::vec3(1, 0, 0));
    ref = glm::rotate(ref, s.rot.y, glm::vec3(0, 1, 0));
    ref = glm::rotate(ref, s.rot.z, glm::vec3(0, 0, 1));
    ref = glm::scale(ref, ToGlmVec3(s.scaleV));

    SCOPED_TRACE("seed pos=(" + std::to_string(s.pos.x) + "," +
                 std::to_string(s.pos.y) + "," + std::to_string(s.pos.z) + ")");
    EXPECT_TRUE(SameMat4(mine, ref, 2e-5f));
  }
}

TEST(MathVsGlm, TransformPointMatchesClipSpace) {
  // Same scene as viewProj + model; transform a vertex and compare clip coords.
  const Vec3 eye(0.0f, 2.0f, 8.0f);
  const float fov = 1.0471976f; // 60 deg
  const Mat4 proj = perspective(fov, 16.0f / 9.0f, 0.1f, 1000.0f);
  const Mat4 view = lookAt(eye, Vec3(0, 0, 0), Vec3(0, 1, 0));

  Mat4 model(1.0f);
  model = translate(model, Vec3(1.0f, 0.5f, -3.0f));
  model = rotate(model, 0.9f, Vec3(1, 0, 0));
  model = scale(model, Vec3(1.5f, 1.5f, 1.5f));

  glm::mat4 gProj = glm::perspective(fov, 16.0f / 9.0f, 0.1f, 1000.0f);
  glm::mat4 gView = glm::lookAt(glm::vec3(0.0f, 2.0f, 8.0f), glm::vec3(0, 0, 0),
                                glm::vec3(0, 1, 0));
  glm::mat4 gModel(1.0f);
  gModel = glm::translate(gModel, glm::vec3(1.0f, 0.5f, -3.0f));
  gModel = glm::rotate(gModel, 0.9f, glm::vec3(1, 0, 0));
  gModel = glm::scale(gModel, glm::vec3(1.5f, 1.5f, 1.5f));

  const Vec4 v(0.4f, -0.2f, 1.1f, 1.0f);

  // Custom pipeline: clip = (proj * view) * model * v
  const Mat4 viewProj = proj * view;
  const Mat4 modelViewProj = viewProj * model;
  const Vec4 myClip = MulMat4Vec4(modelViewProj, v);

  // GLM reference: clip = (proj * view) * model * v
  const glm::vec4 refClip =
      gProj * gView * gModel * glm::vec4(v.x, v.y, v.z, v.w);

  EXPECT_NEAR(myClip.x, refClip[0], kEps);
  EXPECT_NEAR(myClip.y, refClip[1], kEps);
  EXPECT_NEAR(myClip.z, refClip[2], kEps);
  EXPECT_NEAR(myClip.w, refClip[3], kEps);
}

// --- Memory layout -----------------------------------------------------------

TEST(MathVsGlm, ValuePtrColumnMajorLayout) {
  // value_ptr must be usable as a column-major mat4 for glUniformMatrix4fv.
  Mat4 m(0.0f);
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      m[c][r] = 10.0f * c + r;

  const glm::mat4 ref = ToGlm(m);
  for (int i = 0; i < 16; ++i)
    EXPECT_FLOAT_EQ(value_ptr(m)[i], glm::value_ptr(ref)[i]);
}

TEST(MathVsGlm, RadiansDegreesConsistency) {
  EXPECT_NEAR(radians(180.0f), glm::radians(180.0f), kEps);
  EXPECT_NEAR(radians(360.0f), glm::radians(360.0f), kEps);
  EXPECT_NEAR(radians(-90.0f), glm::radians(-90.0f), kEps);
}

// --- Frustum extraction (render-path consumer of the math) -------------------

namespace {

using namespace componeng::renderer;

std::array<FrustumPlane, 6> ExtractGlm(const glm::mat4 &m) {
  std::array<FrustumPlane, 6> p;
  FrustumPlane *planes[] = {&p[0], &p[1], &p[2], &p[3], &p[4], &p[5]};
  const int sign[6] = {1, -1, 1, -1, 1, -1};
  for (int c = 0; c < 3; ++c) {
    for (int k = 0; k < 2; ++k) {
      const int signIdx = c * 2 + k;
      const int s = sign[signIdx];
      auto &pl = *planes[signIdx];
      pl.normal.x = m[0][3] + s * m[0][c];
      pl.normal.y = m[1][3] + s * m[1][c];
      pl.normal.z = m[2][3] + s * m[2][c];
      pl.distance = m[3][3] + s * m[3][c];
    }
  }
  for (auto &pl : p) {
    const float len =
        glm::length(glm::vec3(pl.normal.x, pl.normal.y, pl.normal.z));
    pl.normal /= len;
    pl.distance /= len;
  }
  return p;
}

void ExpectPlanes(const std::array<FrustumPlane, 6> &a,
                  const std::array<FrustumPlane, 6> &b) {
  for (int i = 0; i < 6; ++i) {
    EXPECT_NEAR(a[i].normal.x, b[i].normal.x, kEps) << "plane " << i;
    EXPECT_NEAR(a[i].normal.y, b[i].normal.y, kEps) << "plane " << i;
    EXPECT_NEAR(a[i].normal.z, b[i].normal.z, kEps) << "plane " << i;
    // The far plane's distance is ~1e3, where one float ulp is ~6e-5, so an
    // absolute 1e-5 is below the precision of the value itself (and Release
    // builds use -ffast-math/FMA, which reorders the arithmetic). Compare
    // relatively once the magnitude exceeds 1.
    EXPECT_NEAR(a[i].distance, b[i].distance,
                kEps * std::max(1.0f, std::fabs(b[i].distance)))
        << "plane " << i;
  }
}

} // namespace

TEST(MathVsGlm, FrustumPlaneExtraction) {
  const float fov = 1.0471976f;
  const glm::mat4 gProj = glm::perspective(fov, 16.0f / 9.0f, 0.1f, 1000.0f);
  const glm::mat4 gView =
      glm::lookAt(glm::vec3(0, 2, 8), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
  const glm::mat4 gVP = gProj * gView;

  // Feed the identical matrix (as a custom Mat4) into the engine's Frustum.
  componeng::renderer::Frustum custom(FromGlm(gVP));
  const auto mine = custom.getPlanes();
  const auto ref = ExtractGlm(gVP);

  ExpectPlanes(mine, ref);

  // Same check through the whole custom view/proj pipeline.
  const Mat4 proj = perspective(fov, 16.0f / 9.0f, 0.1f, 1000.0f);
  const Mat4 view = lookAt(Vec3(0, 2, 8), Vec3(0, 0, 0), Vec3(0, 1, 0));
  componeng::renderer::Frustum customPipeline(proj * view);
  ExpectPlanes(customPipeline.getPlanes(), ref);
}

// --- transpose / inverse -----------------------------------------------------

TEST(MathVsGlm, TransposeMatchesGlm) {
  const glm::mat4 g =
      glm::perspective(1.0f, 1.7f, 0.1f, 100.0f) *
      glm::lookAt(glm::vec3(1, 2, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
  EXPECT_TRUE(SameMat4(transpose(FromGlm(g)), glm::transpose(g)));
}

TEST(MathVsGlm, InverseMatchesGlmOnEngineMatrices) {
  const glm::mat4 cases[] = {
      glm::mat4(1.0f),
      glm::translate(glm::mat4(1.0f), glm::vec3(3, -4, 5)),
      glm::scale(glm::mat4(1.0f), glm::vec3(2, 0.5f, 4)),
      glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(0, 1, 0)),
      glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f),
      glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 10000.0f) *
          glm::lookAt(glm::vec3(5, 3, 2), glm::vec3(0, 1, 0),
                      glm::vec3(0, 1, 0)),
  };
  for (const glm::mat4 &g : cases) {
    // Perspective inverses have large entries; compare relatively.
    const Mat4 mine = inverse(FromGlm(g));
    const glm::mat4 ref = glm::inverse(g);
    float worst = 0.0f;
    for (int c = 0; c < 4; ++c)
      for (int r = 0; r < 4; ++r)
        worst = std::max(worst, std::fabs(mine[c][r] - ref[c][r]) /
                                    std::max(1.0f, std::fabs(ref[c][r])));
    EXPECT_LE(worst, 1e-4f);
  }
}

TEST(Math, InverseTimesOriginalIsIdentity) {
  const Mat4 m = perspective(radians(60.0f), 1.5f, 0.1f, 500.0f) *
                 lookAt(Vec3(4, 2, 7), Vec3(0, 0, 0), Vec3(0, 1, 0));
  const Mat4 id = inverse(m) * m;
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_NEAR(id[c][r], c == r ? 1.0f : 0.0f, 1e-3f) << c << "," << r;
}

TEST(Math, InverseOfSingularMatrixIsIdentity) {
  const Mat4 zero(0.0f);
  const Mat4 inv = inverse(zero);
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_EQ(inv[c][r], c == r ? 1.0f : 0.0f);
}
