#include "componeng/core/engine.hpp"
#include "componeng/utils/utils.hpp"
#include "forest_game.hpp"

#include <GLFW/glfw3.h>

int main() {
  // The engine creates its window without asking for multisampling. GLFW keeps
  // window hints once set, so initialising it first and requesting samples here
  // gives a 4x MSAA framebuffer without touching the engine.
  glfwInit();
  glfwWindowHint(GLFW_SAMPLES, 4);

  // The shaders, models and textures are in forest/assets, not the engine tree.
  componeng::utils::Utils::addAssetRoot(FOREST_ROOT_DIR);

  componeng::core::Engine &engine = componeng::core::Engine::get();
  engine.init();

  forest::ForestGame game;
  engine.run(game);

  engine.shutdown();
  return 0;
}
