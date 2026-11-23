#pragma once

#include <cstddef>
#include <cstdint>

namespace componeng::renderer {

/** Uniform-buffer binding point the per-frame block lives at. */
constexpr uint32_t kFrameDataBinding = 0;

/** Name of the uniform block a shader declares to receive FrameData. */
constexpr const char *kFrameDataBlockName = "FrameData";

/**
 * @brief Everything that is constant for a whole frame, uploaded once to a
 * uniform buffer instead of being re-sent to every shader on every switch.
 *
 * A shader opts in by declaring this block (identically in its vertex and
 * fragment stages, since a block shared across stages must match exactly):
 *
 *   layout(std140) uniform FrameData {
 *       mat4 viewProj;
 *       vec3 cameraPos;      float time;
 *       vec3 lightDir;       float fogDensity;
 *       vec3 lightColor;     float fogStart;
 *       vec3 ambientColor;   float ambientStrength;
 *       vec4 fogColor;
 *       float fogEnd;
 *   };
 *
 * Members are then used by bare name (the block is unnamed), exactly as the
 * equivalent loose uniforms would be. A shader that does not declare the block
 * keeps working: the RenderSystem sends those same values as loose uniforms.
 *
 * lightDir points towards the light. This struct mirrors the std140 layout
 * float for float: each vec3 is followed by the scalar that fills its fourth
 * component, which std140 permits and which keeps the struct padding-free
 * apart from the tail.
 */
struct FrameData {
  float viewProj[16];
  float cameraPos[3];
  float time;
  float lightDir[3];
  float fogDensity;
  float lightColor[3];
  float fogStart;
  float ambientColor[3];
  float ambientStrength;
  float fogColor[4];
  float fogEnd;
  float padding[3]; // std140 rounds the block up to a multiple of 16 bytes
};

static_assert(sizeof(FrameData) == 160,
              "FrameData must match its std140 block");
static_assert(offsetof(FrameData, cameraPos) == 64, "std140: after the mat4");
static_assert(offsetof(FrameData, lightDir) == 80, "std140: vec3 + float = 16");
static_assert(offsetof(FrameData, lightColor) == 96,
              "std140: vec3 + float = 16");
static_assert(offsetof(FrameData, ambientColor) == 112,
              "std140: vec3 + float = 16");
static_assert(offsetof(FrameData, fogColor) == 128,
              "std140: vec4 is 16-aligned");
static_assert(offsetof(FrameData, fogEnd) == 144, "std140: scalar after vec4");

} // namespace componeng::renderer
