#pragma once
// GX2 → DXGI format mapping, shared by the future Xbox D3D12 backend.
// This does NOT perform Wii U surface untile, byte swap, or data conversion.
#include <cstdint>
#include <dxgiformat.h>

namespace gfxd3d12 {
struct FormatInfo {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t bytes_per_block = 0;
    uint32_t block_width = 1;
    uint32_t block_height = 1;
    bool supported() const { return format != DXGI_FORMAT_UNKNOWN; }
};
FormatInfo format_info(uint32_t gx2Format, bool isDepth = false);
bool format_mapping_self_test();
} // namespace gfxd3d12
