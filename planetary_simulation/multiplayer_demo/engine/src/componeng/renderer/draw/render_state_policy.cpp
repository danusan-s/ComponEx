#include "componeng/renderer/draw/render_state_policy.hpp"

namespace componeng::renderer {

api::RenderState renderStateFor(api::PassId pass,
                                const api::RenderState &material,
                                bool depthPrepassed) {
  api::RenderState s;
  switch (pass) {
    case api::PassId::DepthPrepass:
      s.cull = material.cull;
      s.depthTest = true;
      s.depthWrite = true;
      s.depthFunc = api::DepthFunc::Less;
      s.blend = api::BlendMode::None;
      s.colorWrite = false;
      return s;

    case api::PassId::Opaque:
      s.cull = material.cull;
      s.depthTest = true;
      s.depthWrite = !depthPrepassed;
      s.depthFunc =
          depthPrepassed ? api::DepthFunc::Equal : api::DepthFunc::Less;
      s.blend = api::BlendMode::None;
      return s;

    case api::PassId::Sky:
      // A full-screen triangle has no winding worth culling. The shader pins
      // depth to exactly 1.0, so the test must accept equal depth, and there
      // is nothing behind the sky to protect, so it does not write depth.
      s.cull = api::CullMode::None;
      s.depthFunc = api::DepthFunc::LessEqual;
      s.depthWrite = false;
      return s;

    case api::PassId::Transparent:
      return material;

    case api::PassId::PostProcess:
      // A full-screen copy onto the window: nothing to test, write or blend.
      s.cull = api::CullMode::None;
      s.depthTest = false;
      s.depthWrite = false;
      s.blend = api::BlendMode::None;
      return s;
  }
  return s;
}

} // namespace componeng::renderer
