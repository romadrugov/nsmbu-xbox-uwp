#pragma once
// SPDX-License-Identifier: MPL-2.0
// GX2 2D Tiled Thin1 RGBA8 decoder. Addressing adapted from the vendored
// Cemu LatteAddrLib_Coord.cpp (Cemu contributors, MPL-2.0).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gfxd3d12 {
constexpr uint32_t kGx2Tiled2DThin1 = 4;

// One 32-bpp color surface, slice=0, sample=0, mip=0, GX2 tile mode 4.
// gx2_swizzle is the raw GX2 surface swizzle (pipe bits 8, bank bits 9-10).
// pitch_texels must be a multiple of 32; source contains full padded
// macro-tile rows (ceil(height/16)*16). Returns tightly packed RGBA8.
// Output is unchanged on invalid or truncated input. There is intentionally
// no default swizzle: the caller must supply the real GX2 descriptor value.
bool detile_macro2d_thin1_rgba8(
    const uint8_t* source, size_t source_bytes,
    uint32_t width, uint32_t height, uint32_t pitch_texels,
    uint32_t gx2_swizzle, std::vector<uint8_t>& output);
}  // namespace gfxd3d12
