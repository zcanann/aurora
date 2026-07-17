#include <gtest/gtest.h>

#include "../lib/gfx/frame_capture.hpp"
#include "../lib/gfx/png_io.hpp"

#include <algorithm>
#include <array>

namespace aurora::gfx::frame_capture::testing {
namespace {

TEST(FrameCaptureRequest, RejectsInvalidArgumentsWithCopyableError) {
  reset();
  EXPECT_FALSE(request(nullptr, 320, 180));
  EXPECT_EQ(status(), AURORA_FRAME_CAPTURE_INVALID_ARGUMENT);
  const size_t errorLength = copy_error(nullptr, 0);
  EXPECT_GT(errorLength, 0u);
  std::array<char, 8> truncated{};
  EXPECT_EQ(copy_error(truncated.data(), truncated.size()), errorLength);
  EXPECT_EQ(truncated.back(), '\0');
  reset();
}

TEST(FrameCaptureResample, PreservesRgbaAndForcesPresentedAlphaOpaque) {
  constexpr std::array<uint8_t, 8> source{10, 20, 30, 0, 40, 50, 60, 127};
  const auto output = resample_rgba8(source.data(), 2, 1, 8, wgpu::TextureFormat::RGBA8Unorm, 2, 1);
  const std::array<uint8_t, 8> expected{10, 20, 30, 255, 40, 50, 60, 255};
  EXPECT_EQ(output, std::vector<uint8_t>(expected.begin(), expected.end()));
}

TEST(FrameCaptureResample, SwizzlesBgraAndBilinearlyDownsamples) {
  constexpr std::array<uint8_t, 16> source{
      0, 0, 0, 0, 0, 0, 100, 0, 0, 100, 0, 0, 100, 0, 0, 0,
  };
  const auto output = resample_rgba8(source.data(), 2, 2, 8, wgpu::TextureFormat::BGRA8Unorm, 1, 1);
  const std::array<uint8_t, 4> expected{25, 25, 25, 255};
  EXPECT_EQ(output, std::vector<uint8_t>(expected.begin(), expected.end()));
}

TEST(FrameCapturePng, EncodesAValidRgba8Image) {
  constexpr std::array<uint8_t, 8> pixels{10, 20, 30, 255, 40, 50, 60, 255};
  std::vector<uint8_t> encoded;
  std::string error;
  ASSERT_TRUE(png::encode_rgba8_png(2, 1, pixels, encoded, error)) << error;
  ASSERT_GE(encoded.size(), 8u);
  constexpr std::array<uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
  EXPECT_TRUE(std::equal(signature.begin(), signature.end(), encoded.begin()));

  const auto decoded = png::parse_png_bytes(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->width, 2u);
  EXPECT_EQ(decoded->height, 1u);
  ASSERT_EQ(decoded->data.size(), pixels.size());
  EXPECT_TRUE(std::equal(pixels.begin(), pixels.end(), decoded->data.data()));
}

} // namespace
} // namespace aurora::gfx::frame_capture::testing
