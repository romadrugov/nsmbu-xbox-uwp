#pragma once
// Single-mip linear RGBA8 upload; caller retains staging until GPU fence.
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>
namespace gfxd3d12 {
struct TextureUpload {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> staging;
    UINT row_pitch = 0;
    UINT64 uploaded_bytes = 0;
};
HRESULT upload_linear_rgba8(ID3D12Device* device,
                            ID3D12GraphicsCommandList* list,
                            const uint8_t* rgba, uint32_t width,
                            uint32_t height, TextureUpload& out);
}
