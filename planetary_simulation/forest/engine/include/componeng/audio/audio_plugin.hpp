#pragma once

#include "componeng/core/plugin.hpp"

namespace componeng::audio {

// The AudioManager resource, the audio component and the AudioSystem. Opens the
// audio engine when ready and closes it on cleanup.
class AudioPlugin final : public core::IPlugin {
public:
  void build(core::Engine &engine) override;
  void ready(ecs::World &world) override;
  void cleanup(ecs::World &world) override;
};

} // namespace componeng::audio
