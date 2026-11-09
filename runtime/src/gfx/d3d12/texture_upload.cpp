#include "texture_upload.h"
#include "format_map.h"
#include <cstring>
#include <utility>

namespace gfxd3d12 {
HRESULT upload_linear_rgba8(ID3D12Device* device,
                            ID3D12GraphicsCommandList* list,
                            const uint8_t* rgba, uint32_t width,
                            uint32_t height, TextureUpload& out) {
    if (!device || !list || !rgba || width == 0 || height == 0 ||
        width > 4096 || height > 4096) return E_INVALIDARG;
    const auto fmt = format_info(0x1a);
    if (!fmt.supported() || fmt.format != DXGI_FORMAT_R8G8B8A8_UNORM ||
        fmt.bytes_per_block != 4 || fmt.block_width != 1 ||
        fmt.block_height != 1) return E_FAIL;
    TextureUpload result;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = fmt.format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = defaultHeap.VisibleNodeMask = 1;
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(result.texture.GetAddressOf()));
    if (FAILED(hr)) return hr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 rowBytes = 0, totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0,
                                  &footprint, nullptr, &rowBytes, &totalBytes);
    if (rowBytes != static_cast<UINT64>(width) * 4 || totalBytes == 0 ||
        footprint.Footprint.RowPitch < rowBytes ||
        (footprint.Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) != 0)
        return E_FAIL;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = totalBytes;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = uploadHeap.VisibleNodeMask = 1;
    hr = device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(result.staging.GetAddressOf()));
    if (FAILED(hr)) return hr;
    void* mapped = nullptr;
    const D3D12_RANGE noCpuReads = {0, 0};
    hr = result.staging->Map(0, &noCpuReads, &mapped);
    if (FAILED(hr)) return hr;
    std::memset(mapped, 0, static_cast<size_t>(totalBytes));
    auto* dst = static_cast<uint8_t*>(mapped) + footprint.Offset;
    for (uint32_t y = 0; y < height; ++y)
        std::memcpy(dst + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                    rgba + static_cast<size_t>(y) * width * 4,
                    static_cast<size_t>(width) * 4);
    result.staging->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = result.staging.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = result.texture.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;
    list->CopyTextureRegion(&dstLoc, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = result.texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1, &barrier);
    result.row_pitch = footprint.Footprint.RowPitch;
    result.uploaded_bytes = totalBytes;
    out = std::move(result);
    return S_OK;
}
}
