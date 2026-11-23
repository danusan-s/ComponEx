#include "componeng/net/archive.hpp"
#include "componeng/net/archive_math.hpp"
#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace componeng::net;
using Bytes = std::vector<uint8_t>;

namespace {

enum class Piece : uint8_t { None, Pawn, Knight, King = 200 };

struct Square {
  uint8_t file = 0;
  uint8_t rank = 0;
  template <typename Ar> void serialize(Ar &ar) {
    ar(file, rank);
  }
  bool operator==(const Square &o) const {
    return file == o.file && rank == o.rank;
  }
};

struct Everything {
  bool flag = false;
  int8_t i8 = 0;
  uint16_t u16 = 0;
  int32_t i32 = 0;
  uint64_t u64 = 0;
  int64_t i64 = 0;
  float f = 0;
  double d = 0;
  Piece piece = Piece::None;
  std::string text;
  std::vector<Square> squares;
  std::vector<uint8_t> blob;
  std::array<uint16_t, 3> triple{};
  componeng::core::Vec3 position;
  componeng::core::Vec2 uv;
  componeng::core::Vec4 color;

  template <typename Ar> void serialize(Ar &ar) {
    ar(flag, i8, u16, i32, u64, i64, f, d, piece, text, squares, blob, triple,
       position, uv, color);
  }
};

Everything sample() {
  Everything e;
  e.flag = true;
  e.i8 = -7;
  e.u16 = 54321;
  e.i32 = -2000000000;
  e.u64 = 0xFEDCBA9876543210ull;
  e.i64 = std::numeric_limits<int64_t>::min();
  e.f = -1.5f;
  e.d = 3.141592653589793;
  e.piece = Piece::King;
  e.text = "knight to f3";
  e.squares = {{4, 1}, {4, 3}, {0, 7}};
  e.blob = {0, 255, 1, 254};
  e.triple = {1, 2, 3};
  e.position = {1.0f, -2.0f, 3.5f};
  e.uv = {0.25f, 0.75f};
  e.color = {1, 0.5f, 0.25f, 0};
  return e;
}

template <typename T> Bytes written(const T &value) {
  return toBytes(value);
}

} // namespace

// The exact bytes are protocol: peers on any CPU or compiler must agree.
TEST(Archive, WireFormatIsLittleEndianAndFixed) {
  EXPECT_EQ(written(uint8_t{0xAB}), (Bytes{0xAB}));
  EXPECT_EQ(written(uint16_t{0x0102}), (Bytes{0x02, 0x01}));
  EXPECT_EQ(written(uint32_t{0x01020304}), (Bytes{0x04, 0x03, 0x02, 0x01}));
  EXPECT_EQ(written(uint64_t{0x0102030405060708ull}),
            (Bytes{8, 7, 6, 5, 4, 3, 2, 1}));
  EXPECT_EQ(written(int8_t{-1}), (Bytes{0xFF}));
  EXPECT_EQ(written(int16_t{-2}), (Bytes{0xFE, 0xFF}));
  EXPECT_EQ(written(int32_t{-1}), (Bytes{0xFF, 0xFF, 0xFF, 0xFF}));
  EXPECT_EQ(written(true), (Bytes{1}));
  EXPECT_EQ(written(false), (Bytes{0}));
  EXPECT_EQ(written(1.0f), (Bytes{0x00, 0x00, 0x80, 0x3F}));
  EXPECT_EQ(written(-2.0), (Bytes{0, 0, 0, 0, 0, 0, 0, 0xC0}));
  EXPECT_EQ(written(Piece::King), (Bytes{200}));
}

TEST(Archive, StringsAndContainersAreLengthPrefixedWithVarints) {
  EXPECT_EQ(written(std::string("hi")), (Bytes{2, 'h', 'i'}));
  EXPECT_EQ(written(std::string()), (Bytes{0}));
  EXPECT_EQ(written(std::vector<uint16_t>{1, 2}),
            (Bytes{2, 0x01, 0x00, 0x02, 0x00}));
  EXPECT_EQ(written(std::vector<uint8_t>{9, 8}), (Bytes{2, 9, 8}));
  EXPECT_EQ(written(std::array<uint8_t, 3>{1, 2, 3}), (Bytes{1, 2, 3}));
  // 300 = 0b10_0101100: low seven bits with the high bit set, then the rest.
  EXPECT_EQ(written(std::string(300, 'x')).size(), 2u + 300u);
  EXPECT_EQ(written(std::string(300, 'x'))[0], 0xAC);
  EXPECT_EQ(written(std::string(300, 'x'))[1], 0x02);
}

TEST(Archive, EngineVectorsAreTheirFloatsInOrder) {
  EXPECT_EQ(written(componeng::core::Vec3(1.0f, 0.0f, -2.0f)),
            (Bytes{0, 0, 0x80, 0x3F, 0, 0, 0, 0, 0, 0, 0, 0xC0}));
}

TEST(Archive, RoundTripsEverySupportedKind) {
  Everything original = sample();
  Bytes bytes = toBytes(original);
  Everything copy;
  ASSERT_TRUE(fromBytes(bytes, copy));
  EXPECT_EQ(copy.flag, original.flag);
  EXPECT_EQ(copy.i8, original.i8);
  EXPECT_EQ(copy.u16, original.u16);
  EXPECT_EQ(copy.i32, original.i32);
  EXPECT_EQ(copy.u64, original.u64);
  EXPECT_EQ(copy.i64, original.i64);
  EXPECT_EQ(copy.f, original.f);
  EXPECT_EQ(copy.d, original.d);
  EXPECT_EQ(copy.piece, original.piece);
  EXPECT_EQ(copy.text, original.text);
  EXPECT_EQ(copy.squares, original.squares);
  EXPECT_EQ(copy.blob, original.blob);
  EXPECT_EQ(copy.triple, original.triple);
  EXPECT_EQ(copy.position.y, original.position.y);
  EXPECT_EQ(copy.uv.x, original.uv.x);
  EXPECT_EQ(copy.color.z, original.color.z);
}

TEST(Archive, SpecialFloatsKeepTheirBits) {
  for (float value : {0.0f, -0.0f, std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::denorm_min(),
                      std::numeric_limits<float>::max()}) {
    float copy = 1.0f;
    ASSERT_TRUE(fromBytes(toBytes(value), copy));
    EXPECT_EQ(std::memcmp(&copy, &value, sizeof(float)), 0);
  }
  // A NaN is compared by its bits: the build may use -ffast-math, under which
  // isnan() is not reliable.
  float nan = std::numeric_limits<float>::quiet_NaN();
  float copy = 0.0f;
  ASSERT_TRUE(fromBytes(toBytes(nan), copy));
  EXPECT_EQ(std::memcmp(&copy, &nan, sizeof(float)), 0);
}

TEST(Archive, EmptyContainersRoundTrip) {
  Everything empty;
  Everything copy = sample();
  ASSERT_TRUE(fromBytes(toBytes(empty), copy));
  EXPECT_TRUE(copy.text.empty());
  EXPECT_TRUE(copy.squares.empty());
  EXPECT_TRUE(copy.blob.empty());
}

TEST(Archive, LargeLengthsRoundTrip) {
  std::string big(100000, 'z');
  std::string copy;
  ASSERT_TRUE(fromBytes(toBytes(big), copy));
  EXPECT_EQ(copy, big);
}

TEST(Archive, ReadingSeveralValuesInOneCall) {
  ByteWriter writer;
  writer(uint8_t{1}, std::string("two"), uint32_t{3});
  ByteReader reader(writer.data());
  uint8_t a = 0;
  std::string b;
  uint32_t c = 0;
  reader(a, b, c);
  EXPECT_FALSE(reader.failed());
  EXPECT_EQ(reader.remaining(), 0u);
  EXPECT_EQ(a, 1);
  EXPECT_EQ(b, "two");
  EXPECT_EQ(c, 3u);
}

TEST(Archive, WritingAConstValueWorks) {
  const Everything value = sample();
  ByteWriter writer;
  writer(value);
  EXPECT_FALSE(writer.failed());
  EXPECT_GT(writer.size(), 0u);
}

// ---- Hostile input ----

TEST(Archive, EveryTruncationOfAMessageFailsCleanly) {
  Bytes bytes = toBytes(sample());
  for (size_t cut = 0; cut < bytes.size(); ++cut) {
    Everything copy;
    EXPECT_FALSE(fromBytes(bytes.data(), cut, copy)) << "cut at " << cut;
  }
}

TEST(Archive, ReadingPastTheEndIsStickyAndZeroes) {
  Bytes bytes = {1, 2};
  ByteReader reader(bytes);
  uint32_t big = 7;
  uint8_t after = 9;
  reader(big);
  EXPECT_TRUE(reader.failed());
  EXPECT_EQ(big, 0u);
  reader(after);
  EXPECT_EQ(after, 0);
}

TEST(Archive, TrailingBytesAreRejectedByFromBytes) {
  Bytes bytes = toBytes(Square{1, 2});
  bytes.push_back(0);
  Square square;
  EXPECT_FALSE(fromBytes(bytes, square));
}

TEST(Archive, BoolOtherThanZeroOrOneIsRejected) {
  Bytes bytes = {2};
  bool value = false;
  EXPECT_FALSE(fromBytes(bytes, value));
}

TEST(Archive, ForgedLengthCannotForceAnAllocation) {
  // Claims 4 billion elements with a handful of bytes behind it.
  Bytes forged = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 1, 2, 3};
  std::vector<Square> squares;
  EXPECT_FALSE(fromBytes(forged, squares));
  EXPECT_TRUE(squares.empty());
  EXPECT_EQ(squares.capacity(), 0u);

  std::string text;
  EXPECT_FALSE(fromBytes(forged, text));
  EXPECT_TRUE(text.empty());

  std::vector<uint8_t> blob;
  EXPECT_FALSE(fromBytes(forged, blob));
  EXPECT_EQ(blob.capacity(), 0u);
}

TEST(Archive, LengthAsLargeAsTheInputAllowsIsAccepted) {
  Bytes ok = {3, 'a', 'b', 'c'};
  std::string text;
  EXPECT_TRUE(fromBytes(ok, text));
  EXPECT_EQ(text, "abc");
  Bytes oneShort = {4, 'a', 'b', 'c'};
  EXPECT_FALSE(fromBytes(oneShort, text));
}

TEST(Archive, MalformedVarintsAreRejected) {
  std::string text;
  // Six continuation bytes: longer than any uint32.
  EXPECT_FALSE(fromBytes(Bytes{0x80, 0x80, 0x80, 0x80, 0x80, 0x00}, text));
  // Five bytes whose last one overflows 32 bits.
  EXPECT_FALSE(fromBytes(Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0x1F}, text));
  // Ends in the middle of one.
  EXPECT_FALSE(fromBytes(Bytes{0x80}, text));
}

TEST(Archive, NestedElementsStopAtTheFirstFailure) {
  Bytes bytes = {2, 1, 1, 2};
  std::vector<Square> squares;
  EXPECT_FALSE(fromBytes(bytes, squares));
}

TEST(Archive, UnknownEnumValuesPassThroughForTheGameToCheck) {
  Bytes bytes = {77};
  Piece piece = Piece::Pawn;
  ASSERT_TRUE(fromBytes(bytes, piece));
  EXPECT_EQ(static_cast<uint8_t>(piece), 77);
}

TEST(Archive, RandomGarbageNeverCrashesTheReader) {
  uint32_t state = 12345;
  auto next = [&state] {
    state = state * 1664525u + 1013904223u;
    return static_cast<uint8_t>(state >> 24);
  };
  for (int round = 0; round < 2000; ++round) {
    Bytes junk(next() % 64);
    for (uint8_t &b : junk) {
      b = next();
    }
    Everything copy;
    fromBytes(junk, copy);
  }
}
