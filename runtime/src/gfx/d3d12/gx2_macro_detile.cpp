// SPDX-License-Identifier: MPL-2.0
// 2D Tiled Thin1 byte address calculation: color, 32-bpp, non-MSAA,
// non-bank-swapped GX2 mode 4. Derived from vendored Cemu
// LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiled (MPL-2.0).
#include "gx2_macro_detile.h"
#include <cstring>
#include <utility>

namespace gfxd3d12 {
namespace {
constexpr uint32_t kBanks = 4;
constexpr uint32_t kPipes = 2;
constexpr uint32_t kMacroWidth = 8 * kBanks;  // 32 texels
constexpr uint32_t kMacroHeight = 8 * kPipes; // 16 texels
constexpr size_t kMacroBytes = size_t(kMacroWidth) * kMacroHeight * 4;
constexpr size_t kPipeInterleave = 256;
constexpr size_t kMaxPixels = 4096ull * 4096;

unsigned pixel_index_32bpp(uint32_t x, uint32_t y) {
    // 32-bit non-depth Cemu microtile bits: x0,x1,y0,x2,y1,y2.
    return (x & 1u) | (x & 2u) | ((y & 1u) << 2u) |
           ((x & 4u) << 1u) | ((y & 2u) << 3u) | ((y & 4u) << 3u);
}

size_t macro_offset_32bpp(uint32_t x, uint32_t y,
                          uint32_t pitch, uint32_t gx2Swizzle) {
    const uint32_t pipe = ((y >> 3u) ^ (x >> 3u)) & 1u;
    const uint32_t bankBits = (y >> 4u) & 3u;
    const uint32_t rawBank = (((bankBits >> 1u) | (bankBits << 1u)) ^
                              (x >> 3u)) & 3u;
    const uint32_t pipeBank = (pipe + kPipes * rawBank) ^
                               ((gx2Swizzle >> 8u) & 7u);
    const size_t macroIndex = size_t(x / kMacroWidth) +
        size_t(pitch / kMacroWidth) * (y / kMacroHeight);
    const size_t macroOffset = macroIndex * kMacroBytes;
    const size_t pixelOffset = pixel_index_32bpp(x, y) * 4u;
    // Original Cemu macro-address interleave: three pipe/bank bits sit
    // between bits 7 and 11 of the byte address.
    const size_t interleaved = (macroOffset >> 3u) + pixelOffset;
    const size_t high = (interleaved & ~(kPipeInterleave - 1u)) << 3u;
    const size_t low = interleaved & (kPipeInterleave - 1u);
    return high | low | (size_t(pipeBank & 1u) << 8u) |
           (size_t(pipeBank >> 1u) << 9u);
}
}  // namespace

bool detile_macro2d_thin1_rgba8(
    const uint8_t* source, size_t source_bytes,
    uint32_t width, uint32_t height, uint32_t pitch,
    uint32_t gx2_swizzle, std::vector<uint8_t>& output) {
    if (!source || !width || !height || width > 4096 || height > 4096 ||
        pitch < width || pitch > 8192 || (pitch % kMacroWidth) != 0 ||
        size_t(width) * height > kMaxPixels)
        return false;

    // A macro tile is 32x16 RGBA8 pixels (2048 bytes). In real GX2 assets
    // use the pitch and image size returned by GX2CalculateSurfaceInfo.
    const size_t paddedHeight = (size_t(height) + 15u) & ~size_t(15u);
    const size_t required = size_t(pitch) * paddedHeight * 4u;
    if (source_bytes < required)
        return false;

    std::vector<uint8_t> decoded(size_t(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t src = macro_offset_32bpp(x, y, pitch, gx2_swizzle);
            if (src > source_bytes || source_bytes - src < 4u)
                return false;
            const size_t dst = (size_t(y) * width + x) * 4u;
            std::memcpy(decoded.data() + dst, source + src, 4u);
        }
    }
    output.swap(decoded);
    return true;
}
}  // namespace gfxd3d12
