#include "componeng/audio/audio_component.hpp"
#include "componeng/audio/audio_manager.hpp"
#include "componeng/audio/audio_system.hpp"
#include "componeng/camera/main_camera.hpp"
#include "componeng/core/transform_component.hpp"
#include "componeng/ecs/world.hpp"
#include "gtest/gtest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace componeng;
using namespace componeng::audio;

namespace {

constexpr int kRate = 48000;

// Writes a mono 16-bit PCM WAV holding a quiet tone `seconds` long.
std::string writeWav(const std::string &name, float seconds) {
  std::string path =
      (std::filesystem::temp_directory_path() / ("componeng_" + name)).string();
  uint32_t frames = static_cast<uint32_t>(seconds * kRate);
  uint32_t dataBytes = frames * 2;
  std::ofstream f(path, std::ios::binary);
  auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<char *>(&v), 4); };
  auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<char *>(&v), 2); };
  f.write("RIFF", 4);
  u32(36 + dataBytes);
  f.write("WAVEfmt ", 8);
  u32(16);
  u16(1);
  u16(1);
  u32(kRate);
  u32(kRate * 2);
  u16(2);
  u16(16);
  f.write("data", 4);
  u32(dataBytes);
  for (uint32_t i = 0; i < frames; ++i) {
    int16_t s = static_cast<int16_t>((i / 50) % 2 ? 2000 : -2000);
    f.write(reinterpret_cast<char *>(&s), 2);
  }
  return path;
}

// Lets `seconds` of audio go by on the headless engine.
void advance(AudioManager &mgr, float seconds) {
  std::vector<float> buffer(2 * 512);
  for (int done = 0; done < seconds * kRate; done += 512) {
    ma_engine_read_pcm_frames(&mgr.getEngine(), buffer.data(), 512, nullptr);
  }
}

struct AudioFixture : ::testing::Test {
  AudioManager mgr;
  std::string blip, longTone;

  void SetUp() override {
    ASSERT_TRUE(mgr.init(true));
    blip = writeWav("blip.wav", 0.2f);
    longTone = writeWav("long.wav", 5.0f);
    ASSERT_TRUE(mgr.registerSound(blip.c_str(), "blip"));
    ASSERT_TRUE(mgr.registerSound(longTone.c_str(), "long"));
  }
  void TearDown() override {
    mgr.shutdown();
    std::filesystem::remove(blip);
    std::filesystem::remove(longTone);
  }
};

} // namespace

TEST_F(AudioFixture, UnknownNamesAndBadPathsAreRejected) {
  EXPECT_FALSE(mgr.registerSound("/no/such/file.wav", "ghost"));
  EXPECT_FALSE(mgr.hasSound("ghost"));
  EXPECT_EQ(mgr.createSound("ghost"), core::INVALID_HANDLE);
  EXPECT_FALSE(mgr.isValid(core::INVALID_HANDLE));
  EXPECT_FALSE(mgr.playSound(core::INVALID_HANDLE));
  EXPECT_FALSE(mgr.stopSound(core::INVALID_HANDLE));
  EXPECT_THROW(mgr.getSound(core::INVALID_HANDLE), std::out_of_range);
}

TEST_F(AudioFixture, PlayAndStopToggleIsPlaying) {
  auto id = mgr.createSound("long", false, false);
  ASSERT_NE(id, core::INVALID_HANDLE);
  EXPECT_FALSE(mgr.isPlaying(id));
  EXPECT_TRUE(mgr.playSound(id));
  EXPECT_TRUE(mgr.isPlaying(id));
  EXPECT_TRUE(mgr.stopSound(id));
  EXPECT_FALSE(mgr.isPlaying(id));
}

TEST_F(AudioFixture, NonSpatializedSoundsIgnoreTheListener) {
  auto flat = mgr.createSound("blip", false);
  auto world = mgr.createSound("blip", true);
  EXPECT_FALSE(
      ma_sound_is_spatialization_enabled(mgr.getSound(flat).getSound()));
  EXPECT_TRUE(
      ma_sound_is_spatialization_enabled(mgr.getSound(world).getSound()));
}

TEST_F(AudioFixture, FireAndForgetInstancesAreReclaimedWhenFinished) {
  auto id = mgr.playOneShot("blip");
  ASSERT_NE(id, core::INVALID_HANDLE);

  mgr.cleanupFinishedSounds();
  EXPECT_TRUE(mgr.isValid(id)); // still playing

  advance(mgr, 0.5f);
  mgr.cleanupFinishedSounds();
  EXPECT_FALSE(mgr.isValid(id));

  // The freed slot is handed out again.
  EXPECT_EQ(mgr.createSound("blip"), id);
}

TEST_F(AudioFixture, ManyOneShotsDoNotGrowWithoutBound) {
  core::HandleID highest = 0;
  for (int i = 0; i < 50; ++i) {
    auto id = mgr.playOneShotAt("blip", 1.0f, 2.0f, 3.0f);
    ASSERT_NE(id, core::INVALID_HANDLE);
    highest = std::max(highest, id);
    advance(mgr, 0.3f);
    mgr.cleanupFinishedSounds();
  }
  EXPECT_EQ(highest, 0u);
}

TEST_F(AudioFixture, OwnedInstancesSurviveTheirEndAndCanReplay) {
  auto id = mgr.createSound("blip", false, false);
  ASSERT_TRUE(mgr.playSound(id));
  advance(mgr, 0.5f);
  mgr.cleanupFinishedSounds();
  ASSERT_TRUE(mgr.isValid(id));
  EXPECT_TRUE(mgr.getSound(id).isFinished());

  EXPECT_TRUE(mgr.playSound(id)); // restarts from the beginning
  EXPECT_TRUE(mgr.isPlaying(id));

  mgr.releaseSound(id);
  EXPECT_FALSE(mgr.isValid(id));
  mgr.releaseSound(id); // releasing twice is harmless
}

TEST_F(AudioFixture, RegisteringANameAgainReplacesIt) {
  EXPECT_TRUE(mgr.registerSound(longTone.c_str(), "blip"));
  auto id = mgr.createSound("blip");
  EXPECT_NE(id, core::INVALID_HANDLE);
}

TEST_F(AudioFixture, StreamedSoundsPlay) {
  ASSERT_TRUE(mgr.registerSound(longTone.c_str(), "streamed", true));
  auto a = mgr.createSound("streamed", false, false);
  auto b = mgr.createSound("streamed", false, false);
  ASSERT_NE(a, core::INVALID_HANDLE);
  ASSERT_NE(b, core::INVALID_HANDLE);
  EXPECT_TRUE(mgr.playSound(a));
  EXPECT_TRUE(mgr.playSound(b));
  advance(mgr, 0.1f);
  EXPECT_TRUE(mgr.isPlaying(a));
}

TEST(AudioManagerTest, UninitializedManagerFailsSoftly) {
  AudioManager mgr;
  EXPECT_FALSE(mgr.isInitialized());
  EXPECT_FALSE(mgr.registerSound("x.wav", "x"));
  EXPECT_EQ(mgr.createSound("x"), core::INVALID_HANDLE);
  EXPECT_EQ(mgr.playOneShot("x"), core::INVALID_HANDLE);
  mgr.setMasterVolume(0.5f);
  mgr.cleanupFinishedSounds();
  mgr.shutdown();
}

// --- AudioSystem: the component's `play` flag drives the sound --------------

namespace {

struct SystemScene : ::testing::Test {
  std::unique_ptr<ecs::World> world = std::make_unique<ecs::World>();
  AudioSystem system;
  ecs::EntityID entity = ecs::INVALID_ENTITY;
  std::string blip, longTone;

  void SetUp() override {
    world->registerComponents<core::TransformComponent, AudioComponent>();
    ecs::EntityID cam = world->createEntity();
    world->addComponents(cam, core::TransformComponent{});
    world->setResource(camera::MainCamera{cam});
    // The system looks the manager up as a resource. A started ma_engine
    // cannot be moved, so it is initialized where it will live.
    world->setResource<AudioManager>(AudioManager());
    ASSERT_TRUE(manager().init(true));
    blip = writeWav("sys_blip.wav", 0.2f);
    longTone = writeWav("sys_long.wav", 5.0f);
    ASSERT_TRUE(manager().registerSound(blip.c_str(), "blip"));
    ASSERT_TRUE(manager().registerSound(longTone.c_str(), "long"));
  }
  void TearDown() override {
    world->getResource<AudioManager>().shutdown();
    std::filesystem::remove(blip);
    std::filesystem::remove(longTone);
  }
  AudioManager &manager() {
    return world->getResource<AudioManager>();
  }
  AudioComponent &audio() {
    return world->getComponent<AudioComponent>(entity);
  }
  void spawn(AudioComponent component) {
    entity = world->createEntity();
    world->addComponents(entity, core::TransformComponent{},
                         AudioComponent(component));
  }
  void tick(float seconds = 0.0f) {
    system.onUpdate({world.get(), 0.016f});
    if (seconds > 0.0f)
      advance(manager(), seconds);
  }
};

} // namespace

TEST_F(SystemScene, PlayOnAwakeStartsTheSound) {
  spawn(AudioComponent{.audioName = "long", .playOnAwake = true, .loop = true});
  tick();
  EXPECT_TRUE(audio().play);
  EXPECT_TRUE(audio().isPlaying);
  EXPECT_TRUE(manager().isPlaying(audio().audioID));
}

TEST_F(SystemScene, WithoutPlayOnAwakeItStaysSilentUntilAsked) {
  spawn(AudioComponent{.audioName = "long", .playOnAwake = false});
  tick();
  EXPECT_FALSE(audio().isPlaying);

  audio().play = true;
  tick();
  EXPECT_TRUE(audio().isPlaying);

  audio().play = false;
  tick();
  EXPECT_FALSE(audio().isPlaying);
  EXPECT_FALSE(manager().isPlaying(audio().audioID));
}

TEST_F(SystemScene, StoppingPausesAndAskingAgainResumes) {
  spawn(
      AudioComponent{.audioName = "long", .playOnAwake = false, .loop = true});
  for (int i = 0; i < 3; ++i) {
    audio().play = true;
    tick(0.05f);
    EXPECT_TRUE(manager().isPlaying(audio().audioID));
    audio().play = false;
    tick(0.05f);
    EXPECT_FALSE(manager().isPlaying(audio().audioID));
  }
}

TEST_F(SystemScene, ASoundThatEndsClearsPlayAndCanBeTriggeredAgain) {
  spawn(
      AudioComponent{.audioName = "blip", .playOnAwake = true, .loop = false});
  tick(0.5f);
  tick(); // notices the sound has ended
  EXPECT_FALSE(audio().play);
  EXPECT_FALSE(audio().isPlaying);

  tick(0.1f); // and does not start it over by itself
  EXPECT_FALSE(manager().isPlaying(audio().audioID));

  audio().play = true;
  tick();
  EXPECT_TRUE(audio().isPlaying);
}

TEST_F(SystemScene, AmbientSoundsDoNotAccumulateInstances) {
  spawn(AudioComponent{.audioName = "long", .playOnAwake = true, .loop = true});
  for (int i = 0; i < 100; ++i)
    tick();
  EXPECT_EQ(audio().audioID, 0u);
  // The one instance is the only slot ever used: the next create takes slot 1.
  EXPECT_EQ(manager().createSound("blip"), 1u);
}

TEST_F(SystemScene, AnUnregisteredNameWaitsInsteadOfFailing) {
  spawn(
      AudioComponent{.audioName = "later", .playOnAwake = true, .loop = true});
  tick();
  EXPECT_EQ(audio().audioID, core::INVALID_HANDLE);

  ASSERT_TRUE(manager().registerSound(longTone.c_str(), "later"));
  tick();
  EXPECT_NE(audio().audioID, core::INVALID_HANDLE);
  EXPECT_TRUE(audio().isPlaying);
}
