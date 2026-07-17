#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "common.hpp"
#include "texture_convert.hpp"

namespace aurora::gfx::png {
std::optional<ConvertedTexture> parse_png_bytes(ArrayRef<uint8_t> bytes) noexcept;
std::optional<ConvertedTexture> load_png_file(const std::filesystem::path& path) noexcept;
bool encode_rgba8_png(uint32_t width, uint32_t height, ArrayRef<uint8_t> pixels,
                      std::vector<uint8_t>& output, std::string& error) noexcept;
}
