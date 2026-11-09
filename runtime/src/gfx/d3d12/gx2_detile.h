#pragma once
// CPU-side decoding of a bounded subset of Wii U GX2 surface layouts.
// Applies to 32-bit RGBA8, one slice and one mip level, no MSAA.
// Modes and micro-tile bit order cross-checked with the vendored Cemu LatteAddrLib.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gfxd3d12 {
constexpr uint32_t kGx2LinearAligned = 1;
constexpr uint32_t kGx2Tiled1DThin1 = 2;

// pitch_texels is the horizontal *surface pitch*, not necessarily the visible width.
// On success, result is tightly packed (width * height * 4) in scanline order.
// On failure, result is untouched. Unknown tile modes are rejected, never guessed.
bool detile_rgba8(const uint8_t* source, size_t source_bytes,
                  uint32_t width, uint32_t height, uint32_t pitch_texels,
                  uint32_t gx2_tile_mode, std::vector<uint8_t>& result);

// CPU-only cross-checks: known 32bpp micro-tile positions, padded linear pitch,
// multiple tiles, truncated buffers and unsupported modes.
bool detile_rgba8_self_test();
} // namespace gfxd3d12
