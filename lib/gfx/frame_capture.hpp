#pragma once

#include <aurora/aurora.h>

#include "../webgpu/wgpu.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aurora::gfx::frame_capture {

bool request(const char* path, uint32_t width, uint32_t height) noexcept;
bool pending() noexcept;
void encode_if_requested(const wgpu::CommandEncoder& encoder) noexcept;
void after_submit_and_wait() noexcept;
AuroraFrameCaptureStatus status() noexcept;
size_t copy_error(char* destination, size_t capacity) noexcept;
void reset() noexcept;

namespace testing {
std::vector<uint8_t> resample_rgba8(const uint8_t* source, uint32_t sourceWidth, uint32_t sourceHeight,
                                    uint32_t sourceBytesPerRow, wgpu::TextureFormat sourceFormat, uint32_t outputWidth,
                                    uint32_t outputHeight);
}

} // namespace aurora::gfx::frame_capture
