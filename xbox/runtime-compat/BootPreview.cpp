// R5F visual preview for the existing Xbox UWP boot shell.
//
// Displays an optional owner-provided PNG from LocalState/nsmbu_preview.png.
// If the PNG is absent, a synthetic test pattern is displayed explicitly.
// No Nintendo assets, no guest entrypoint, no GX2 calls and no game execution.
// D3D12 initialization, texture upload and swap-chain synchronization follow
// the physical Xbox-tested xbox/smoke/MainGpu.cpp path.
#include "BootPreview.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "gfx/d3d12/texture_upload.h"
#include "BootPreviewVS.h"
#include "BootPreviewPS.h"

using Microsoft::WRL::ComPtr;

namespace nsmbuvisual {
namespace {

constexpr UINT kWidth = 1280;
constexpr UINT kHeight = 720;
constexpr UINT kBufferCount = 2;

void Check(HRESULT hr, const char* action) {
    if (SUCCEEDED(hr)) return;
    char line[240] = {};
    sprintf_s(line, "R5F %s HRESULT=0x%08lX",
              action, static_cast<unsigned long>(hr));
    throw std::runtime_error(line);
}

std::vector<uint8_t> SyntheticPixels(uint32_t& width, uint32_t& height) {
    width = 256;
    height = 144;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const bool dark = ((x / 32) ^ (y / 32)) & 1;
            const size_t at = (static_cast<size_t>(y) * width + x) * 4;
            pixels[at] = dark ? 25 : 90;
            pixels[at + 1] = dark ? 55 : 147;
            pixels[at + 2] = dark ? 84 : 192;
            pixels[at + 3] = 255;
        }
    }
    return pixels;
}

// WIC runs only against an owner-local file. No image bytes enter GitHub/AppX.
// The 2048x2048 limit bounds allocations before the decode/copy operation.
bool ReadLocalPng(const wchar_t* path, std::vector<uint8_t>& rgba,
                  uint32_t& width, uint32_t& height) {
    if (!path || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return false;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))))
        return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
              WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())))
        return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return false;
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) ||
        w < 16 || h < 16 || w > 2048 || h > 2048)
        return false;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())))
        return false;
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
              WICBitmapDitherTypeNone, nullptr, 0.0,
              WICBitmapPaletteTypeCustom)))
        return false;
    const uint32_t pitch = w * 4;
    std::vector<uint8_t> output(static_cast<size_t>(pitch) * h);
    if (FAILED(converter->CopyPixels(nullptr, pitch,
              static_cast<UINT>(output.size()), output.data())))
        return false;
    width = w;
    height = h;
    rgba.swap(output);
    return true;
}

} // anonymous namespace

struct Preview::Impl final {
    explicit Impl(IUnknown* window, const wchar_t* path, Logger logger)
        : log(logger) {
        uint32_t w = 0, h = 0;
        std::vector<uint8_t> pixels;
        localImage = ReadLocalPng(path, pixels, w, h);
        if (!localImage) {
            pixels = SyntheticPixels(w, h);
            log("R5F PNG absent/unsupported: displaying SYNTHETIC pattern (not Nintendo image)");
        } else {
            char msg[160] = {};
            sprintf_s(msg, "R5F LocalState/nsmbu_preview.png decoded: %ux%u RGBA8", w, h);
            log(msg);
        }
        Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
              IID_PPV_ARGS(device.GetAddressOf())), "D3D12CreateDevice");
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&queueDesc,
              IID_PPV_ARGS(queue.GetAddressOf())), "CreateCommandQueue");
        ComPtr<IDXGIFactory2> factory;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())),
              "CreateDXGIFactory1");
        DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
        swapDesc.Width = kWidth;
        swapDesc.Height = kHeight;
        swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = kBufferCount;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapDesc.Scaling = DXGI_SCALING_STRETCH;
        ComPtr<IDXGISwapChain1> chain;
        Check(factory->CreateSwapChainForCoreWindow(queue.Get(), window,
              &swapDesc, nullptr, chain.GetAddressOf()),
              "CreateSwapChainForCoreWindow");
        Check(chain.As(&swap), "IDXGISwapChain3");

        D3D12_DESCRIPTOR_HEAP_DESC heap = {};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = kBufferCount;
        Check(device->CreateDescriptorHeap(&heap,
              IID_PPV_ARGS(rtvHeap.GetAddressOf())), "RTV heap");
        auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        const UINT stride = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT i = 0; i < kBufferCount; ++i) {
            Check(swap->GetBuffer(i, IID_PPV_ARGS(buffers[i].GetAddressOf())),
                  "GetBuffer");
            device->CreateRenderTargetView(buffers[i].Get(), nullptr, rtv);
            rtv.ptr += stride;
        }
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
              IID_PPV_ARGS(allocator.GetAddressOf())), "CreateCommandAllocator");
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
              allocator.Get(), nullptr, IID_PPV_ARGS(commands.GetAddressOf())),
              "CreateCommandList");
        Check(commands->Close(), "Initial CommandList Close");
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
              IID_PPV_ARGS(fence.GetAddressOf())), "CreateFence");
        fenceEvent = CreateEventExW(nullptr, nullptr, 0,
                                   EVENT_MODIFY_STATE | SYNCHRONIZE);
        if (!fenceEvent) throw std::runtime_error("R5F CreateEventExW failed");
        InitPipeline();
        Upload(pixels, w, h);
        log(localImage ? "R5F GPU READY (owner-local image)" :
                         "R5F GPU READY (synthetic visual fallback)");
    }

    ~Impl() {
        if (fenceEvent) CloseHandle(fenceEvent);
    }

    void WaitGpu() {
        const UINT64 value = ++fenceValue;
        Check(queue->Signal(fence.Get(), value), "Queue Signal");
        if (fence->GetCompletedValue() < value) {
            Check(fence->SetEventOnCompletion(value, fenceEvent),
                  "Fence SetEventOnCompletion");
            if (WaitForSingleObjectEx(fenceEvent, INFINITE, FALSE) != WAIT_OBJECT_0)
                throw std::runtime_error("R5F GPU fence wait failed");
        }
    }

    void Upload(const std::vector<uint8_t>& pixels, uint32_t w, uint32_t h) {
        Check(allocator->Reset(), "Upload allocator Reset");
        Check(commands->Reset(allocator.Get(), nullptr),
              "Upload command list Reset");
        gfxd3d12::TextureUpload upload;
        Check(gfxd3d12::upload_linear_rgba8(device.Get(), commands.Get(),
              pixels.data(), w, h, upload), "Upload linear RGBA8");
        Check(commands->Close(), "Upload CommandList Close");
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        WaitGpu(); // Keep upload.staging alive until the fence has completed.
        texture = upload.texture;
        D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = 1;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device->CreateDescriptorHeap(&srvDesc,
              IID_PPV_ARGS(srvHeap.GetAddressOf())), "SRV heap");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(texture.Get(), &srv,
              srvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    void InitPipeline() {
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER root = {};
        root.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root.DescriptorTable.NumDescriptorRanges = 1;
        root.DescriptorTable.pDescriptorRanges = &range;
        root.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC signature = {};
        signature.NumParameters = 1;
        signature.pParameters = &root;
        signature.NumStaticSamplers = 1;
        signature.pStaticSamplers = &sampler;
        signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> serialized, errors;
        Check(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1,
              serialized.GetAddressOf(), errors.GetAddressOf()),
              "D3D12SerializeRootSignature");
        Check(device->CreateRootSignature(0, serialized->GetBufferPointer(),
              serialized->GetBufferSize(),
              IID_PPV_ARGS(rootSignature.GetAddressOf())), "RootSignature");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rootSignature.Get();
        desc.VS = {gBootPreviewVS, sizeof(gBootPreviewVS)};
        desc.PS = {gBootPreviewPS, sizeof(gBootPreviewPS)};
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        Check(device->CreateGraphicsPipelineState(&desc,
              IID_PPV_ARGS(pipeline.GetAddressOf())), "GraphicsPipeline");
    }

    void Present() {
        const UINT i = swap->GetCurrentBackBufferIndex();
        Check(allocator->Reset(), "Frame allocator Reset");
        Check(commands->Reset(allocator.Get(), nullptr), "Frame list Reset");
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffers[i].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commands->ResourceBarrier(1, &barrier);
        auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += i * device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        const float background[4] = {0.05f, 0.12f, 0.20f, 1.0f};
        commands->ClearRenderTargetView(rtv, background, 0, nullptr);
        commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const D3D12_VIEWPORT viewport = {0.f, 0.f, float(kWidth), float(kHeight), 0.f, 1.f};
        const D3D12_RECT scissor = {0, 0, LONG(kWidth), LONG(kHeight)};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->SetPipelineState(pipeline.Get());
        commands->SetGraphicsRootSignature(rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootDescriptorTable(
            0, srvHeap->GetGPUDescriptorHandleForHeapStart());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->DrawInstanced(6, 1, 0, 0);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commands->ResourceBarrier(1, &barrier);
        Check(commands->Close(), "Frame list Close");
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        Check(swap->Present(1, 0), "SwapChain Present");
        WaitGpu();
        log(localImage ? "R5F PRESENT PASS: owner-local image, NOT gameplay" :
                         "R5F PRESENT PASS: synthetic pattern, NOT gameplay");
    }

    Logger log;
    bool localImage = false;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swap;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12Resource> buffers[kBufferCount];
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipeline;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
};

Preview::Preview(IUnknown* window, const wchar_t* path, Logger logger)
    : impl_(new Impl(window, path, logger)) {}
Preview::~Preview() = default;
void Preview::present() { impl_->Present(); }
bool Preview::loaded_local_image() const noexcept { return impl_->localImage; }

} // namespace nsmbuvisual
