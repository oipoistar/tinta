#ifndef TINTA_IMAGE_LOADER_H
#define TINTA_IMAGE_LOADER_H

#include <windows.h>
#include <wincodec.h>
#include <cstdint>
#include <vector>

// Bound the display copy, independently of compressed file size. Originals
// remain untouched for HTML/DOCX export. Also reject extreme source dimensions
// before asking a codec to allocate its internal decoding buffers (#220).
constexpr uint64_t IMAGE_DECODE_MAX_PIXELS = 16ull * 1024 * 1024;
constexpr uint64_t IMAGE_SOURCE_MAX_PIXELS = 100000000;
constexpr UINT IMAGE_SOURCE_MAX_DIMENSION = 65535;
constexpr UINT IMAGE_DECODE_MAX_DIMENSION = 8192;

struct ImageDecodeSize { UINT width = 0, height = 0; };
ImageDecodeSize boundedImageSize(UINT width, UINT height, UINT maxDimension);

struct DecodedImage {
    UINT width = 0, height = 0;
    float dpiX = 96.0f, dpiY = 96.0f;
    std::vector<uint8_t> pixels;  // premultiplied BGRA
};

// No partially decoded pixels escape on failure, including allocation failure.
HRESULT decodeImageFile(IWICImagingFactory* factory, const wchar_t* path,
                       UINT maxDimension, DecodedImage& output) noexcept;

#endif
