#include "format_map.h"

namespace gfxd3d12 {
namespace {
FormatInfo scalar(DXGI_FORMAT format, uint32_t bytes) {
    return {format, bytes, 1, 1};
}
FormatInfo bc(DXGI_FORMAT format, uint32_t bytes) {
    return {format, bytes, 4, 4};
}
}

// GX2 numeric format values and flag masks intentionally match the Vulkan
// reference mapping in runtime/src/gfx/vulkan/formats.cpp.
FormatInfo format_info(uint32_t fmt, bool depth) {
    const uint32_t hw = fmt & 0x3fu;
    const bool integer = (fmt & 0x100u) != 0;
    const bool signedValue = (fmt & 0x200u) != 0;
    const bool srgb = (fmt & 0x400u) != 0;
    if (depth) {
        switch (hw) {
        case 0x05: return scalar(DXGI_FORMAT_D16_UNORM, 2);
        case 0x0e: return scalar(DXGI_FORMAT_D32_FLOAT, 4);
        // D24S8 and D32S8 require explicit surface conversions / typeless
        // resource/view management; do not pretend they work yet.
        default: return {};
        }
    }

    // Signed-only without integer flag denotes SNORM in the GX2 mapping.
    const bool sint = integer && signedValue;
    const bool uint = integer && !signedValue;
    const bool snorm = !integer && signedValue;
    if (srgb && (integer || signedValue)) return {};

    switch (hw) {
    case 0x01:
        return scalar(sint ? DXGI_FORMAT_R8_SINT :
                      uint ? DXGI_FORMAT_R8_UINT :
                      snorm ? DXGI_FORMAT_R8_SNORM : DXGI_FORMAT_R8_UNORM, 1);
    case 0x05:
        return scalar(sint ? DXGI_FORMAT_R16_SINT :
                      uint ? DXGI_FORMAT_R16_UINT :
                      snorm ? DXGI_FORMAT_R16_SNORM : DXGI_FORMAT_R16_UNORM, 2);
    case 0x06:
        return (!integer && !signedValue) ? scalar(DXGI_FORMAT_R16_FLOAT, 2) : FormatInfo{};
    case 0x07:
        return scalar(sint ? DXGI_FORMAT_R8G8_SINT :
                      uint ? DXGI_FORMAT_R8G8_UINT :
                      snorm ? DXGI_FORMAT_R8G8_SNORM : DXGI_FORMAT_R8G8_UNORM, 2);
    case 0x0e:
        return (!signedValue && !srgb)
            ? scalar(integer ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R32_FLOAT, 4)
            : FormatInfo{};
    case 0x0f:
        return scalar(sint ? DXGI_FORMAT_R16G16_SINT :
                      uint ? DXGI_FORMAT_R16G16_UINT :
                      snorm ? DXGI_FORMAT_R16G16_SNORM : DXGI_FORMAT_R16G16_UNORM, 4);
    case 0x10:
        return (!integer && !signedValue) ? scalar(DXGI_FORMAT_R16G16_FLOAT, 4) : FormatInfo{};
    case 0x1a:
        return scalar(srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB :
                      sint ? DXGI_FORMAT_R8G8B8A8_SINT :
                      uint ? DXGI_FORMAT_R8G8B8A8_UINT :
                      snorm ? DXGI_FORMAT_R8G8B8A8_SNORM : DXGI_FORMAT_R8G8B8A8_UNORM, 4);
    case 0x20:
        return (!integer && !signedValue) ? scalar(DXGI_FORMAT_R16G16B16A16_FLOAT, 8) : FormatInfo{};
    case 0x23:
        return (!integer && !signedValue) ? scalar(DXGI_FORMAT_R32G32B32A32_FLOAT, 16) : FormatInfo{};
    case 0x31:
        return (!integer && !signedValue)
            ? bc(srgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM, 8) : FormatInfo{};
    case 0x32:
        return (!integer && !signedValue)
            ? bc(srgb ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM, 16) : FormatInfo{};
    case 0x33:
        return (!integer && !signedValue)
            ? bc(srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM, 16) : FormatInfo{};
    case 0x34:
        return (!integer && !srgb) ? bc(signedValue ? DXGI_FORMAT_BC4_SNORM : DXGI_FORMAT_BC4_UNORM, 8) : FormatInfo{};
    case 0x35:
        return (!integer && !srgb) ? bc(signedValue ? DXGI_FORMAT_BC5_SNORM : DXGI_FORMAT_BC5_UNORM, 16) : FormatInfo{};
    default:
        // Some formats (RGB565, RG4, D24S8, etc.) require CPU conversion.
        return {};
    }
}

bool format_mapping_self_test() {
    const auto rgba8 = format_info(0x1a);
    const auto srgb = format_info(0x41a);
    const auto bc1 = format_info(0x31);
    const auto bc5s = format_info(0x235);
    const auto depth = format_info(0x05, true);
    const auto unsupported = format_info(0x08);
    return rgba8.format == DXGI_FORMAT_R8G8B8A8_UNORM &&
           rgba8.bytes_per_block == 4 && rgba8.block_width == 1 &&
           srgb.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
           bc1.format == DXGI_FORMAT_BC1_UNORM && bc1.bytes_per_block == 8 &&
           bc1.block_width == 4 && bc1.block_height == 4 &&
           bc5s.format == DXGI_FORMAT_BC5_SNORM &&
           depth.format == DXGI_FORMAT_D16_UNORM &&
           !unsupported.supported();
}
} // namespace gfxd3d12
