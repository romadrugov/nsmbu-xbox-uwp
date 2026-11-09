#include "gx2_detile.h"

#include <array>
#include <cstring>
#include <utility>

namespace gfxd3d12 {
namespace {

// Cemu LatteAddrLib_Coord.cpp: _ComputePixelIndexWithinMicroTile
// (microTileType=0, 32 bits-per-pixel, thin tiles).
// Bit positions: x0, x1, y0, x2, y1, y2.
size_t thin1_pixel_index(uint32_t x, uint32_t y) {
    return (x & 1u) | (x & 2u) | ((y & 1u) << 2u) |
           ((x & 4u) << 1u) | ((y & 2u) << 3u) | ((y & 4u) << 3u);
}

bool valid_dimensions(uint32_t w, uint32_t h, uint32_t pitch) {
    // Bound CPU work and allocation to 64 MiB; caller must provide full pitch.
    return w > 0 && h > 0 && w <= 4096 && h <= 4096 &&
           pitch >= w && pitch <= 8192;
}

} // namespace

bool detile_rgba8(const uint8_t* source, size_t source_bytes,
                  uint32_t width, uint32_t height, uint32_t pitch,
                  uint32_t mode, std::vector<uint8_t>& result) {
    if (!source || !valid_dimensions(width, height, pitch)) return false;
    size_t required = 0;
    if (mode == kGx2LinearAligned) {
        required = static_cast<size_t>(pitch) * height * 4;
    } else if (mode == kGx2Tiled1DThin1) {
        if ((pitch & 7u) != 0) return false;
        // An 8x8 micro tile of RGBA8 is exactly 256 bytes. Height rounds up
        // to whole micro-tile rows; pitch is already 8-texel aligned.
        required = static_cast<size_t>(pitch / 8) * ((height + 7u) / 8u) * 256;
    } else {
        return false; // Not a macro-tile decoder and no default-mode inference.
    }
    if (source_bytes < required) return false;

    std::vector<uint8_t> output(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t srcPixel = 0;
            if (mode == kGx2LinearAligned) {
                srcPixel = static_cast<size_t>(y) * pitch + x;
            } else {
                const size_t tile = static_cast<size_t>(y / 8) * (pitch / 8) + x / 8;
                srcPixel = tile * 64 + thin1_pixel_index(x, y);
            }
            const size_t src = srcPixel * 4;
            const size_t dst = (static_cast<size_t>(y) * width + x) * 4;
            if (src > source_bytes || source_bytes - src < 4) return false;
            std::memcpy(output.data() + dst, source + src, 4);
        }
    }
    result.swap(output);
    return true;
}

bool detile_rgba8_self_test() {
    // Independent fixed lookup table for 32bpp, 8x8 Cemu thin micro-tiles.
    // Keeping this table explicit catches accidental changes in bit positions.
    constexpr std::array<uint8_t, 64> kIndices = {
         0, 1, 2, 3, 8, 9,10,11,
         4, 5, 6, 7,12,13,14,15,
        16,17,18,19,24,25,26,27,
        20,21,22,23,28,29,30,31,
        32,33,34,35,40,41,42,43,
        36,37,38,39,44,45,46,47,
        48,49,50,51,56,57,58,59,
        52,53,54,55,60,61,62,63
    };

    constexpr uint32_t width = 13, height = 9, pitch = 16;
    std::vector<uint8_t> expected(width * height * 4);
    std::vector<uint8_t> tiled((pitch / 8) * 2 * 256, 0);
    std::vector<uint8_t> linear(pitch * height * 4, 0);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t pixel[4] = {
                static_cast<uint8_t>(x * 17),
                static_cast<uint8_t>(y * 23),
                static_cast<uint8_t>((x ^ y) * 13), 255
            };
            std::memcpy(expected.data() + (y * width + x) * 4, pixel, 4);
            std::memcpy(linear.data() + (y * pitch + x) * 4, pixel, 4);
            const size_t tileIndex = (y / 8) * (pitch / 8) + x / 8;
            const size_t element = kIndices[(y % 8) * 8 + x % 8];
            std::memcpy(tiled.data() + (tileIndex * 64 + element) * 4, pixel, 4);
        }
    }
    std::vector<uint8_t> decoded;
    if (!detile_rgba8(linear.data(), linear.size(), width, height, pitch,
                      kGx2LinearAligned, decoded) || decoded != expected)
        return false;
    if (!detile_rgba8(tiled.data(), tiled.size(), width, height, pitch,
                      kGx2Tiled1DThin1, decoded) || decoded != expected)
        return false;

    // Sentinel ensures a rejected decode cannot silently replace valid data.
    const auto saved = decoded;
    if (detile_rgba8(tiled.data(), tiled.size() - 1, width, height, pitch,
                     kGx2Tiled1DThin1, decoded)) return false;
    if (detile_rgba8(tiled.data(), tiled.size(), width, height, pitch,
                     4, decoded)) return false; // macro tile unsupported
    if (detile_rgba8(tiled.data(), tiled.size(), width, height, 15,
                     kGx2Tiled1DThin1, decoded)) return false;
    return decoded == saved;
}
} // namespace gfxd3d12
