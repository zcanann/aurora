#include "png_io.hpp"

#include <cstring>

#include "dds_io.hpp"
#include "png.h"
#include "../fs_helper.hpp"

static aurora::Module Log("aurora::gfx::png");

namespace aurora::gfx::png {

struct PngStructs {
  png_structp pStruct = nullptr;
  png_infop pInfo = nullptr;

  ~PngStructs() {
    png_destroy_read_struct(&pStruct, &pInfo, nullptr);
  }
};

struct MemoryCursor {
  ArrayRef<uint8_t> bytes;
  size_t pos = 0;
};

static void readPngData(png_structp png, png_bytep data, const size_t length) {
  auto* cursor = static_cast<MemoryCursor*>(png_get_io_ptr(png));
  if (length > cursor->bytes.size() - cursor->pos) {
    png_error(png, "unexpected end of data");
  }
  std::memcpy(data, cursor->bytes.data() + cursor->pos, length);
  cursor->pos += length;
}

std::optional<ConvertedTexture> parse_png_bytes(ArrayRef<uint8_t> bytes) noexcept {
  PngStructs structs{};
  MemoryCursor cursor{bytes};

  structs.pStruct = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!structs.pStruct) {
    Log.error("png_create_read_struct failed");
    return std::nullopt;
  }

  structs.pInfo = png_create_info_struct(structs.pStruct);
  if (!structs.pInfo) {
    Log.error("png_create_info_struct failed");
    return std::nullopt;
  }

  // I'm scared of putting any locals after that setjmp.
  std::vector<png_bytep> rowPointers;
  ByteBuffer imageData{};
  png_uint_32 width, height;
  int bit_depth, color_type, interlace_type, compression_type, filter_type;
  size_t rowBytes;
  int i;

  if (setjmp(png_jmpbuf(structs.pStruct))) {
    Log.error("libpng encountered an error");
    return std::nullopt;
  }

  png_set_read_fn(structs.pStruct, &cursor, readPngData);
  png_read_info(structs.pStruct, structs.pInfo);

  if (!png_get_IHDR(structs.pStruct, structs.pInfo, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_type)) {
    Log.error("libpng unable to read IHDR");
    return std::nullopt;
  }

  // Always read as RGBA8.
  png_set_gray_to_rgb(structs.pStruct);
  png_set_filler(structs.pStruct, 0xFF, PNG_FILLER_AFTER);
  png_set_expand(structs.pStruct);
  png_set_strip_16(structs.pStruct);

  png_read_update_info(structs.pStruct, structs.pInfo);
  rowBytes = png_get_rowbytes(structs.pStruct, structs.pInfo);
  rowPointers.resize(height);

  imageData.append_zeroes(rowBytes * height);

  for (i = 0; i < height; i++) {
    rowPointers[i] = imageData.data() + i * rowBytes;
  }

  png_read_image(structs.pStruct, rowPointers.data());
  png_read_end(structs.pStruct, nullptr);

  return ConvertedTexture{
    .format = wgpu::TextureFormat::RGBA8Unorm,
    .width = width,
    .height = height,
    .mips = 1,
    .data = std::move(imageData)
  };
}

std::optional<ConvertedTexture>
load_png_file(const std::filesystem::path& path) noexcept {
  const auto bytes = dds::read_binary_file(path);
  if (!bytes.has_value()) {
    Log.error("failed to open file: {}", fs_path_to_string(path));
    return std::nullopt;
  }
  return parse_png_bytes(*bytes);
}

bool encode_rgba8_png(uint32_t width, uint32_t height, ArrayRef<uint8_t> pixels,
                      std::vector<uint8_t>& output, std::string& error) noexcept {
  output.clear();
  error.clear();
  if (width == 0 || height == 0 ||
      static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4 != pixels.size()) {
    error = "invalid RGBA8 image dimensions";
    return false;
  }

  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = width;
  image.height = height;
  image.format = PNG_FORMAT_RGBA;

  png_alloc_size_t encodedSize = 0;
  if (!png_image_write_to_memory(&image, nullptr, &encodedSize, 0, pixels.data(), 0, nullptr)) {
    error = image.message;
    png_image_free(&image);
    return false;
  }

  try {
    output.resize(encodedSize);
  } catch (const std::exception& exception) {
    error = exception.what();
    png_image_free(&image);
    return false;
  }
  if (!png_image_write_to_memory(&image, output.data(), &encodedSize, 0, pixels.data(), 0, nullptr)) {
    error = image.message;
    output.clear();
    png_image_free(&image);
    return false;
  }
  output.resize(encodedSize);
  png_image_free(&image);
  return true;
}
}
