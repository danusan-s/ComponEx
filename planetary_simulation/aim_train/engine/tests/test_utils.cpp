#include "componeng/utils/utils.hpp"

#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>

using componeng::utils::Utils;
namespace fs = std::filesystem;

TEST(AssetPath, FallsBackToTheEngineRoot) {
  // white.png ships with the engine, so it resolves with no extra root.
  std::string path = Utils::getAssetPath("assets/textures/white.png");
  EXPECT_TRUE(fs::exists(path));
}

TEST(AssetPath, AddedRootIsSearchedFirst) {
  fs::path root = fs::temp_directory_path() / "componeng_asset_root_test";
  fs::create_directories(root / "assets" / "shaders");
  std::ofstream(root / "assets" / "shaders" / "game_only.vert") << "// test";

  Utils::addAssetRoot(root.string());

  std::string game = Utils::getAssetPath("assets/shaders/game_only.vert");
  EXPECT_EQ(
      game,
      fs::weakly_canonical(root / "assets/shaders/game_only.vert").string());
  EXPECT_TRUE(fs::exists(game));

  // Files the extra root lacks still come from the engine.
  std::string engine = Utils::getAssetPath("assets/textures/white.png");
  EXPECT_TRUE(fs::exists(engine));
  EXPECT_EQ(engine.find(root.string()), std::string::npos);

  fs::remove_all(root);
}
