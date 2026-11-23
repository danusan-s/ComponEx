#pragma once

#include "componeng/core/types.hpp"
#include "componeng/renderer/backend/api/irender_device.hpp"
#include "componeng/renderer/frame_data.hpp"

#include <memory>
#include <string>

namespace componeng::renderer {

/** Name of the macro defined in the fragment stage of an alpha-tested
 *  material's depth pre-pass program (Shader::alphaTestedDepthVariant). */
inline constexpr const char *kDepthPrepassDefine = "COMPONENG_DEPTH_PREPASS";

/** `source` with `#define <name> 1` on the line after its `#version`
 *  directive (GLSL needs `#version` first), or at the very top if it has none.
 */
std::string defineAfterVersion(const std::string &source, const char *name);

class Shader {
public:
  /** Creates the backend shader through `device`. */
  explicit Shader(api::IRenderDevice &device);

  void use() const;
  void compile(const char *vertexSource, const char *fragmentSource,
               const char *geometrySource = nullptr);

  void setFloat(const char *name, float value) const;
  void setInteger(const char *name, int value) const;
  void setVector2f(const char *name, float x, float y) const;
  void setVector2f(const char *name, const core::Vec2 &value) const;
  void setVector3f(const char *name, float x, float y, float z) const;
  void setVector3f(const char *name, const core::Vec3 &value) const;
  void setVector4f(const char *name, float x, float y, float z, float w) const;
  void setVector4f(const char *name, const core::Vec4 &value) const;
  void setMatrix4(const char *name, const core::Mat4 &matrix) const;

  /** Attaches the shader's uniform block `blockName` to uniform-buffer
   *  binding point `binding`. False if the shader declares no such block. */
  bool bindUniformBlock(const char *blockName, uint32_t binding) const;

  /** Per-shader GPU timing (see IShader::beginGpuTimer). Time the work
   *  submitted between begin and end against this shader; call
   *  collectGpuTimer() once per frame to close it and read finished results. */
  void beginGpuTimer() const;
  void endGpuTimer() const;
  void collectGpuTimer() const;
  /** GPU milliseconds this shader used in the latest completed frame, or
   *  negative if no result is available yet. Lags by a few frames. */
  double lastGpuTimeMs() const;

  /** This shader's vertex (and geometry) stage linked to a fragment shader
   *  that does nothing, for the depth pre-pass. Because the vertex stage is
   *  the same source, it takes the same attributes, uniforms and blocks as
   *  this shader and computes the same gl_Position (declare
   *  `invariant gl_Position;` so that is bit-exact). Compiled on first use. */
  const Shader &depthVariant() const;

  /** The depth pre-pass program for a material whose fragment shader
   *  `discard`s (MaterialType::Cutout). The same vertex stage, linked to this
   *  shader's own fragment stage compiled with `COMPONENG_DEPTH_PREPASS`
   *  defined, so the discard decision is the very code the colour pass runs
   *  and the two cannot disagree. A fragment shader should return right after
   *  its discard when the macro is defined, so the pre-pass skips the lighting:
   *
   *      if (alpha < cutoff) discard;
   *      #ifdef COMPONENG_DEPTH_PREPASS
   *      return;
   *      #endif
   *
   *  Without that early-out it is still correct, only as costly as the colour
   *  pass (colour writes are masked off). Compiled on first use. */
  const Shader &alphaTestedDepthVariant() const;

  api::VertexLayout reflectInstanceLayout() const;
  std::vector<core::Name> reflectActiveUniformNames() const;

private:
  /** Logs an error for each per-frame value the shader reads as a loose
   *  uniform; see FrameUniforms. */
  void warnIfLooseFrameUniforms() const;

  api::IRenderDevice *m_device;
  std::unique_ptr<api::IShader> m_impl;

  // Kept so the depth variants can recompile these stages later.
  std::string m_vertexSource;
  std::string m_geometrySource;
  std::string m_fragmentSource;
  bool m_hasGeometry = false;
  mutable std::unique_ptr<Shader> m_depthVariant;
  mutable std::unique_ptr<Shader> m_alphaTestedDepthVariant;
};

} // namespace componeng::renderer
