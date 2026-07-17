#include "frame_capture.hpp"

#include "../fs_helper.hpp"
#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "png_io.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <magic_enum.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace aurora::gfx::frame_capture {
namespace {

Module Log("aurora::gfx::frame_capture");

constexpr uint32_t MaxOutputDimension = 4096;
constexpr uint64_t MaxOutputPixels = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t MapTimeoutNanoseconds = 30ULL * 1000ULL * 1000ULL * 1000ULL;

struct CaptureState {
  std::mutex mutex;
  AuroraFrameCaptureStatus status = AURORA_FRAME_CAPTURE_IDLE;
  std::filesystem::path path;
  std::string error;
  uint32_t outputWidth = 0;
  uint32_t outputHeight = 0;
  uint32_t sourceWidth = 0;
  uint32_t sourceHeight = 0;
  uint32_t sourceBytesPerRow = 0;
  wgpu::TextureFormat sourceFormat = wgpu::TextureFormat::Undefined;
  wgpu::Buffer readback;
  uint64_t readbackSize = 0;
  bool copyEncoded = false;
};

CaptureState g_capture;

void clear_request_locked() noexcept {
  g_capture.status = AURORA_FRAME_CAPTURE_IDLE;
  g_capture.path.clear();
  g_capture.error.clear();
  g_capture.outputWidth = 0;
  g_capture.outputHeight = 0;
  g_capture.sourceWidth = 0;
  g_capture.sourceHeight = 0;
  g_capture.sourceBytesPerRow = 0;
  g_capture.sourceFormat = wgpu::TextureFormat::Undefined;
  g_capture.readback = {};
  g_capture.readbackSize = 0;
  g_capture.copyEncoded = false;
}

void set_terminal(AuroraFrameCaptureStatus status, std::string error = {}) noexcept {
  std::lock_guard lock{g_capture.mutex};
  g_capture.status = status;
  g_capture.error = std::move(error);
  g_capture.readback = {};
  g_capture.readbackSize = 0;
  g_capture.copyEncoded = false;
}

std::filesystem::path path_from_utf8(const char* path) {
  return std::filesystem::path{reinterpret_cast<const char8_t*>(path)};
}

bool replace_file(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                  std::string& error) {
#if defined(_WIN32)
  if (MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return true;
  }
  error = std::system_category().message(static_cast<int>(GetLastError()));
  return false;
#else
  std::error_code filesystemError;
  std::filesystem::rename(temporary, destination, filesystemError);
  if (!filesystemError) {
    return true;
  }
  error = filesystemError.message();
  return false;
#endif
}

bool write_atomic(const std::filesystem::path& path, const std::vector<uint8_t>& bytes, std::string& error) {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  std::filesystem::path temporary = path;
  temporary += fmt::format(".tmp-{:x}", static_cast<uint64_t>(nonce));
  std::error_code filesystemError;
  std::filesystem::remove(temporary, filesystemError);

  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = fmt::format("cannot open temporary file {}", fs_path_to_string(temporary));
      return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
      error = fmt::format("cannot write temporary file {}", fs_path_to_string(temporary));
      output.close();
      std::filesystem::remove(temporary, filesystemError);
      return false;
    }
  }

  if (!replace_file(temporary, path, error)) {
    std::filesystem::remove(temporary, filesystemError);
    return false;
  }
  return true;
}

uint8_t source_channel(const uint8_t* pixel, wgpu::TextureFormat format, uint32_t channel) noexcept {
  if (format == wgpu::TextureFormat::BGRA8Unorm) {
    if (channel == 0) {
      return pixel[2];
    }
    if (channel == 2) {
      return pixel[0];
    }
  }
  return pixel[channel];
}

} // namespace

namespace testing {

std::vector<uint8_t> resample_rgba8(const uint8_t* source, uint32_t sourceWidth, uint32_t sourceHeight,
                                    uint32_t sourceBytesPerRow, wgpu::TextureFormat sourceFormat, uint32_t outputWidth,
                                    uint32_t outputHeight) {
  if (source == nullptr || sourceWidth == 0 || sourceHeight == 0 || outputWidth == 0 || outputHeight == 0 ||
      sourceBytesPerRow < sourceWidth * 4 ||
      (sourceFormat != wgpu::TextureFormat::RGBA8Unorm && sourceFormat != wgpu::TextureFormat::BGRA8Unorm)) {
    return {};
  }

  std::vector<uint8_t> output(static_cast<size_t>(outputWidth) * outputHeight * 4);
  for (uint32_t outputY = 0; outputY < outputHeight; ++outputY) {
    const double sourceY = std::clamp((static_cast<double>(outputY) + 0.5) * sourceHeight / outputHeight - 0.5, 0.0,
                                      static_cast<double>(sourceHeight - 1));
    const auto y0 = static_cast<uint32_t>(std::floor(sourceY));
    const auto y1 = std::min(y0 + 1, sourceHeight - 1);
    const double yWeight = sourceY - y0;
    for (uint32_t outputX = 0; outputX < outputWidth; ++outputX) {
      const double sourceX = std::clamp((static_cast<double>(outputX) + 0.5) * sourceWidth / outputWidth - 0.5, 0.0,
                                        static_cast<double>(sourceWidth - 1));
      const auto x0 = static_cast<uint32_t>(std::floor(sourceX));
      const auto x1 = std::min(x0 + 1, sourceWidth - 1);
      const double xWeight = sourceX - x0;
      const uint8_t* pixels[4]{
          source + static_cast<size_t>(y0) * sourceBytesPerRow + x0 * 4,
          source + static_cast<size_t>(y0) * sourceBytesPerRow + x1 * 4,
          source + static_cast<size_t>(y1) * sourceBytesPerRow + x0 * 4,
          source + static_cast<size_t>(y1) * sourceBytesPerRow + x1 * 4,
      };
      uint8_t* destination = output.data() + (static_cast<size_t>(outputY) * outputWidth + outputX) * 4;
      for (uint32_t channel = 0; channel < 3; ++channel) {
        const double top = source_channel(pixels[0], sourceFormat, channel) * (1.0 - xWeight) +
                           source_channel(pixels[1], sourceFormat, channel) * xWeight;
        const double bottom = source_channel(pixels[2], sourceFormat, channel) * (1.0 - xWeight) +
                              source_channel(pixels[3], sourceFormat, channel) * xWeight;
        destination[channel] =
            static_cast<uint8_t>(std::clamp(std::lround(top * (1.0 - yWeight) + bottom * yWeight), 0L, 255L));
      }
      // The EFB alpha channel is not presented by the desktop swapchain and is commonly zero.
      destination[3] = 0xff;
    }
  }
  return output;
}

} // namespace testing

bool request(const char* path, uint32_t width, uint32_t height) noexcept {
  std::lock_guard lock{g_capture.mutex};
  if (g_capture.status == AURORA_FRAME_CAPTURE_PENDING) {
    g_capture.error = "a frame capture is already pending";
    return false;
  }

  clear_request_locked();
  if (path == nullptr || path[0] == '\0' || width == 0 || height == 0 || width > MaxOutputDimension ||
      height > MaxOutputDimension || static_cast<uint64_t>(width) * height > MaxOutputPixels) {
    g_capture.status = AURORA_FRAME_CAPTURE_INVALID_ARGUMENT;
    g_capture.error = "capture path and dimensions must name a non-empty PNG up to 4096x4096";
    return false;
  }
  if (!webgpu::g_device || webgpu::g_backendType == wgpu::BackendType::Null) {
    g_capture.status = AURORA_FRAME_CAPTURE_UNSUPPORTED;
    g_capture.error = "frame capture requires an initialized non-null graphics backend";
    return false;
  }

  try {
    std::error_code filesystemError;
    auto outputPath = std::filesystem::absolute(path_from_utf8(path), filesystemError).lexically_normal();
    if (filesystemError || outputPath.filename().empty()) {
      g_capture.status = AURORA_FRAME_CAPTURE_INVALID_ARGUMENT;
      g_capture.error = filesystemError ? filesystemError.message() : "capture path has no filename";
      return false;
    }
    const auto parent = outputPath.parent_path();
    if (!std::filesystem::is_directory(parent, filesystemError) || filesystemError) {
      g_capture.status = AURORA_FRAME_CAPTURE_INVALID_ARGUMENT;
      g_capture.error = filesystemError ? filesystemError.message() : "capture parent directory does not exist";
      return false;
    }
    g_capture.path = std::move(outputPath);
  } catch (const std::exception& exception) {
    g_capture.status = AURORA_FRAME_CAPTURE_INVALID_ARGUMENT;
    g_capture.error = exception.what();
    return false;
  }

  g_capture.outputWidth = width;
  g_capture.outputHeight = height;
  g_capture.status = AURORA_FRAME_CAPTURE_PENDING;
  return true;
}

bool pending() noexcept {
  std::lock_guard lock{g_capture.mutex};
  return g_capture.status == AURORA_FRAME_CAPTURE_PENDING;
}

void encode_if_requested(const wgpu::CommandEncoder& encoder) noexcept {
  std::lock_guard lock{g_capture.mutex};
  if (g_capture.status != AURORA_FRAME_CAPTURE_PENDING || g_capture.copyEncoded) {
    return;
  }

  const auto& source = webgpu::present_source();
  if (!source.texture || source.size.width == 0 || source.size.height == 0 ||
      (source.format != wgpu::TextureFormat::RGBA8Unorm && source.format != wgpu::TextureFormat::BGRA8Unorm)) {
    g_capture.status = AURORA_FRAME_CAPTURE_UNSUPPORTED;
    g_capture.error = "resolved EFB is unavailable or not RGBA8/BGRA8";
    return;
  }
  const uint64_t unpaddedBytesPerRow = static_cast<uint64_t>(source.size.width) * 4;
  const uint64_t paddedBytesPerRow = (unpaddedBytesPerRow + 255) & ~uint64_t{255};
  if (paddedBytesPerRow > std::numeric_limits<uint32_t>::max()) {
    g_capture.status = AURORA_FRAME_CAPTURE_GPU_ERROR;
    g_capture.error = "resolved EFB row is too large to read back";
    return;
  }

  g_capture.sourceWidth = source.size.width;
  g_capture.sourceHeight = source.size.height;
  g_capture.sourceBytesPerRow = static_cast<uint32_t>(paddedBytesPerRow);
  g_capture.sourceFormat = source.format;
  g_capture.readbackSize = static_cast<uint64_t>(g_capture.sourceBytesPerRow) * source.size.height;
  const wgpu::BufferDescriptor bufferDescriptor{
      .label = "Frame capture readback",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead,
      .size = g_capture.readbackSize,
  };
  g_capture.readback = webgpu::g_device.CreateBuffer(&bufferDescriptor);
  if (!g_capture.readback) {
    g_capture.status = AURORA_FRAME_CAPTURE_GPU_ERROR;
    g_capture.error = "failed to allocate frame capture readback buffer";
    return;
  }

  const wgpu::TexelCopyTextureInfo sourceInfo{
      .texture = source.texture,
  };
  const wgpu::TexelCopyBufferInfo destinationInfo{
      .layout =
          {
              .offset = 0,
              .bytesPerRow = g_capture.sourceBytesPerRow,
              .rowsPerImage = g_capture.sourceHeight,
          },
      .buffer = g_capture.readback,
  };
  const wgpu::Extent3D extent{
      .width = g_capture.sourceWidth,
      .height = g_capture.sourceHeight,
      .depthOrArrayLayers = 1,
  };
  encoder.CopyTextureToBuffer(&sourceInfo, &destinationInfo, &extent);
  g_capture.copyEncoded = true;
}

void after_submit_and_wait_impl() {
  wgpu::Buffer readback;
  uint64_t readbackSize = 0;
  uint32_t sourceWidth = 0;
  uint32_t sourceHeight = 0;
  uint32_t sourceBytesPerRow = 0;
  uint32_t outputWidth = 0;
  uint32_t outputHeight = 0;
  wgpu::TextureFormat sourceFormat = wgpu::TextureFormat::Undefined;
  std::filesystem::path path;
  {
    std::lock_guard lock{g_capture.mutex};
    if (g_capture.status != AURORA_FRAME_CAPTURE_PENDING || !g_capture.copyEncoded) {
      return;
    }
    readback = g_capture.readback;
    readbackSize = g_capture.readbackSize;
    sourceWidth = g_capture.sourceWidth;
    sourceHeight = g_capture.sourceHeight;
    sourceBytesPerRow = g_capture.sourceBytesPerRow;
    sourceFormat = g_capture.sourceFormat;
    outputWidth = g_capture.outputWidth;
    outputHeight = g_capture.outputHeight;
    path = g_capture.path;
  }

  struct MapResult {
    std::optional<wgpu::MapAsyncStatus> status;
    std::string message;
  };
  auto mapResult = std::make_shared<MapResult>();
  const auto future = readback.MapAsync(wgpu::MapMode::Read, 0, readbackSize, wgpu::CallbackMode::WaitAnyOnly,
                                        [mapResult](wgpu::MapAsyncStatus status, wgpu::StringView message) {
                                          mapResult->status = status;
                                          mapResult->message = std::string{std::string_view{message}};
                                        });
  const auto waitStatus = webgpu::g_instance.WaitAny(future, MapTimeoutNanoseconds);
  if (waitStatus != wgpu::WaitStatus::Success || mapResult->status != wgpu::MapAsyncStatus::Success) {
    readback.Destroy();
    set_terminal(AURORA_FRAME_CAPTURE_GPU_ERROR,
                 fmt::format("frame capture readback failed (wait={}, map={}): {}", magic_enum::enum_name(waitStatus),
                             mapResult->status.has_value() ? magic_enum::enum_name(*mapResult->status)
                                                           : std::string_view{"no callback"},
                             mapResult->message));
    return;
  }

  const auto* mapped = static_cast<const uint8_t*>(readback.GetConstMappedRange(0, readbackSize));
  if (mapped == nullptr) {
    readback.Unmap();
    set_terminal(AURORA_FRAME_CAPTURE_GPU_ERROR, "frame capture readback mapped no bytes");
    return;
  }

  std::vector<uint8_t> resized;
  try {
    resized = testing::resample_rgba8(mapped, sourceWidth, sourceHeight, sourceBytesPerRow, sourceFormat, outputWidth,
                                      outputHeight);
  } catch (const std::exception& exception) {
    readback.Unmap();
    set_terminal(AURORA_FRAME_CAPTURE_GPU_ERROR, exception.what());
    return;
  }
  readback.Unmap();
  if (resized.empty()) {
    set_terminal(AURORA_FRAME_CAPTURE_GPU_ERROR, "frame capture resampling failed");
    return;
  }

  std::vector<uint8_t> encoded;
  std::string error;
  if (!png::encode_rgba8_png(outputWidth, outputHeight, resized, encoded, error)) {
    set_terminal(AURORA_FRAME_CAPTURE_PNG_ERROR, std::move(error));
    return;
  }
  if (!write_atomic(path, encoded, error)) {
    set_terminal(AURORA_FRAME_CAPTURE_IO_ERROR, std::move(error));
    return;
  }

  Log.info("Captured resolved frame to {} ({}x{})", fs_path_to_string(path), outputWidth, outputHeight);
  set_terminal(AURORA_FRAME_CAPTURE_SUCCEEDED);
}

void after_submit_and_wait() noexcept {
  try {
    after_submit_and_wait_impl();
  } catch (const std::exception& exception) {
    set_terminal(AURORA_FRAME_CAPTURE_IO_ERROR, exception.what());
  } catch (...) { set_terminal(AURORA_FRAME_CAPTURE_IO_ERROR, "unknown frame capture failure"); }
}

AuroraFrameCaptureStatus status() noexcept {
  std::lock_guard lock{g_capture.mutex};
  return g_capture.status;
}

size_t copy_error(char* destination, size_t capacity) noexcept {
  std::lock_guard lock{g_capture.mutex};
  const size_t length = g_capture.error.size();
  if (destination != nullptr && capacity > 0) {
    const size_t copied = std::min(length, capacity - 1);
    std::memcpy(destination, g_capture.error.data(), copied);
    destination[copied] = '\0';
  }
  return length;
}

void reset() noexcept {
  std::lock_guard lock{g_capture.mutex};
  clear_request_locked();
}

} // namespace aurora::gfx::frame_capture
