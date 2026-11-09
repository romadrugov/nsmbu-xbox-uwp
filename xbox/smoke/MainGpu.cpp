// D3D12 Xbox probe based on the verified v0.1.4 lifecycle. No Nintendo game data.
// Success: a full-screen background alternates teal shades about once per second.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <cstring>
#include <exception>
#include "gfx/d3d12/format_map.h"
#include "gfx/d3d12/texture_upload.h"
#include "gfx/d3d12/gx2_detile.h"
#include "gfx/d3d12/gx2_macro_detile.h"
#include "gfx/d3d12/backend_probe.h"
#include "core/xbox_guest_vm.h"
#include "ppc_fixture_bridge.h"
#include "RpxProbe.h"

// renderer.cpp is not linked into the UWP smoke test; production builds
// provide this symbol themselves. Do not define it in backend_probe.cpp.
namespace render { const Backend* g_backend = nullptr; }
#include <array>
#include <climits>
#include "GpuShadersVS.h"
#include "GpuShadersPS.h"
using Microsoft::WRL::ComPtr;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

// The Clang-compiled function is generated from recomp/android_fixture.py's
// tiny open-source PowerPC RPX and linked into this MSVC UWP executable.
extern "C" int nsmbu_fixture_return_42(void);

namespace NSMBUXboxSmoke {
constexpr UINT kBufferCount = 2;
HANDLE gLogFile = INVALID_HANDLE_VALUE;

void Log(const char* text) noexcept {
    OutputDebugStringA(text);
    OutputDebugStringA("\n");
    if (gLogFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(gLogFile, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
        WriteFile(gLogFile, "\r\n", 2, &written, nullptr);
        FlushFileBuffers(gLogFile);
    }
}

void OpenLog() noexcept {
    try {
        auto localFolder = Windows::Storage::ApplicationData::Current->LocalFolder;
        std::wstring path(localFolder->Path->Data());
        path += L"\\nsmbu_xbox_diag.txt";
        gLogFile = CreateFile2(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                               CREATE_ALWAYS, nullptr);
        if (gLogFile == INVALID_HANDLE_VALUE) {
            OutputDebugStringA("NSMBU Xbox: could not open diagnostic file\n");
        }
    } catch (...) {
        OutputDebugStringA("NSMBU Xbox: failed to locate LocalState\n");
    }
}



void RunPpcMemoryProbe() noexcept {
    // This reserves 4 GiB of VIRTUAL ADDRESS SPACE. It never commits 4 GiB,
    // which could exceed Xbox UWP app RAM limits. Only one 64 KiB guest slice
    // is committed and checked. Use the PPC_MEM_BASE from runtime/include/ppc.h.
    constexpr SIZE_T kGuestRange = static_cast<SIZE_T>(0x100000000ull);
    constexpr SIZE_T kGuestOffset = static_cast<SIZE_T>(0x10000000ull);
    constexpr SIZE_T kCommit = 64 * 1024;
    void* const fixedAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(0x200000000000ull));
    bool fixed = true;
    bool writeWatch = true;
    void* region = VirtualAllocFromApp(fixedAddress, kGuestRange,
                                       MEM_RESERVE | MEM_WRITE_WATCH, PAGE_NOACCESS);
    if (!region) {
        const DWORD error = GetLastError();
        char line[180] = {};
        sprintf_s(line, "PPC memory FIXED 4GiB reserve with WRITE_WATCH FAILED: Win32=%lu",
                  static_cast<unsigned long>(error));
        Log(line);
        writeWatch = false;
        region = VirtualAllocFromApp(fixedAddress, kGuestRange,
                                     MEM_RESERVE, PAGE_NOACCESS);
    }
    if (!region) {
        const DWORD error = GetLastError();
        char line[170] = {};
        sprintf_s(line, "PPC memory FIXED 4GiB reserve FAILED: Win32=%lu",
                  static_cast<unsigned long>(error));
        Log(line);
        fixed = false;
        region = VirtualAllocFromApp(nullptr, kGuestRange,
                                     MEM_RESERVE, PAGE_NOACCESS);
    }
    if (!region) {
        const DWORD error = GetLastError();
        char line[170] = {};
        sprintf_s(line, "PPC memory ANYWHERE 4GiB reserve FAILED: Win32=%lu",
                  static_cast<unsigned long>(error));
        Log(line);
        return;
    }
    {
        char line[200] = {};
        sprintf_s(line, "PPC memory 4GiB virtual reservation PASSED: %s, write-watch=%s, base=%p",
                  fixed ? "FIXED" : "ANYWHERE", writeWatch ? "yes" : "no", region);
        Log(line);
    }
    auto* guest = static_cast<unsigned char*>(region) + kGuestOffset;
    auto* page = static_cast<unsigned char*>(
        VirtualAllocFromApp(guest, kCommit, MEM_COMMIT, PAGE_READWRITE));
    if (!page) {
        char line[140] = {};
        sprintf_s(line, "PPC memory 64KiB commit FAILED: Win32=%lu",
                  static_cast<unsigned long>(GetLastError()));
        Log(line);
    } else {
        page[0] = 0x42;
        page[kCommit - 1] = 0x99;
        if (page[0] == 0x42 && page[kCommit - 1] == 0x99)
            Log("PPC memory 64KiB guest slice read/write PASSED");
        else
            Log("PPC memory guest slice read/write FAILED");
    }
    if (!VirtualFree(region, 0, MEM_RELEASE)) {
        char line[140] = {};
        sprintf_s(line, "PPC memory VirtualFree FAILED: Win32=%lu",
                  static_cast<unsigned long>(GetLastError()));
        Log(line);
    }
}

void RunTranslatedPpcFixtureProbe() noexcept {
    Log("PPC fixture: calling generated native x64 code from synthetic 3-instruction RPX");
    const int value = nsmbu_fixture_return_42();
    char message[140] = {};
    sprintf_s(message, "PPC fixture generated code RESULT: %d (%s)", value,
              value == 42 ? "PASSED" : "FAILED");
    Log(message);
}


void Check(HRESULT hr, const wchar_t* operation) {
    // Avoid synchronous file writes every frame; record errors only.
    if (SUCCEEDED(hr)) return;
    char message[256] = {};
    char name[160] = {};
    WideCharToMultiByte(CP_UTF8, 0, operation, -1, name, sizeof(name), nullptr, nullptr);
    sprintf_s(message, "%s: 0x%08lX", name, static_cast<unsigned long>(hr));
    Log(message);
    throw std::runtime_error(message);
}

class D3D12Smoke final {
public:
    explicit D3D12Smoke(IUnknown* coreWindow) {
        Log("GPU stage 1: D3D12CreateDevice");
        Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                IID_PPV_ARGS(device_.GetAddressOf())), L"D3D12CreateDevice");

        Log("GPU stage 1 OK; stage 2: CreateCommandQueue");
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue_.GetAddressOf())),
              L"CreateCommandQueue");

        Log("GPU stage 2 OK; stage 3: CreateDXGIFactory");
        ComPtr<IDXGIFactory2> factory;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())), L"CreateDXGIFactory1");

        DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
        swapDesc.Width = 1280;
        swapDesc.Height = 720;
        swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = kBufferCount;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapDesc.Scaling = DXGI_SCALING_STRETCH;

        Log("GPU stage 3 OK; stage 4: CreateSwapChainForCoreWindow");
        ComPtr<IDXGISwapChain1> chain;
        Check(factory->CreateSwapChainForCoreWindow(
                  queue_.Get(), coreWindow, &swapDesc, nullptr, chain.GetAddressOf()),
              L"CreateSwapChainForCoreWindow");
        Check(chain.As(&swapChain_), L"IDXGISwapChain3");

        Log("GPU stage 4 OK; stage 5: GPU render objects");
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = kBufferCount;
        Check(device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf())),
              L"CreateDescriptorHeap");
        auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        const UINT increment = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT i = 0; i < kBufferCount; ++i) {
            Check(swapChain_->GetBuffer(i, IID_PPV_ARGS(buffers_[i].GetAddressOf())),
                  L"GetBuffer");
            device_->CreateRenderTargetView(buffers_[i].Get(), nullptr, rtv);
            rtv.ptr += increment;
        }

        Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(allocator_.GetAddressOf())),
              L"CreateCommandAllocator");
        Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(),
                                         nullptr, IID_PPV_ARGS(list_.GetAddressOf())),
              L"CreateCommandList");
        Check(list_->Close(), L"Initial Close");
        Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf())),
              L"CreateFence");
        fenceEvent_ = CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
        if (!fenceEvent_) {
            throw std::runtime_error("CreateEventExW failed");
        }
        Log("Building D3D12 shader pipeline");
        InitPipeline();
        Log("D3D12 shader pipeline initialized");
        Log("Preparing checker texture and upload buffer");
        InitTexture();
    }

    ~D3D12Smoke() {
        if (fenceEvent_) CloseHandle(fenceEvent_);
    }

    void Draw(const float rgba[4], bool drawQuad) {
        const UINT index = swapChain_->GetCurrentBackBufferIndex();
        Check(allocator_->Reset(), L"Allocator Reset");
        Check(list_->Reset(allocator_.Get(), nullptr), L"CommandList Reset");

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffers_[index].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list_->ResourceBarrier(1, &barrier);

        auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        // Color comes through the real render::Backend command interface.
        list_->ClearRenderTargetView(handle, rgba, 0, nullptr);
        list_->OMSetRenderTargets(1, &handle, FALSE, nullptr);
        if (drawQuad) {
        const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f};
        const D3D12_RECT scissor = {0, 0, 1280, 720};
        list_->RSSetViewports(1, &viewport);
        list_->RSSetScissorRects(1, &scissor);
        list_->SetPipelineState(pipeline_.Get());
        list_->SetGraphicsRootSignature(rootSignature_.Get());
        ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
        list_->SetDescriptorHeaps(1, heaps);
        list_->SetGraphicsRootDescriptorTable(0, srvHeap_->GetGPUDescriptorHandleForHeapStart());
        list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list_->DrawInstanced(6, 1, 0, 0);
        }

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        list_->ResourceBarrier(1, &barrier);
        Check(list_->Close(), L"CommandList Close");

        ID3D12CommandList* commands[] = {list_.Get()};
        queue_->ExecuteCommandLists(1, commands);
        Check(swapChain_->Present(1, 0), L"Present");

        // One frame in flight for this small test; no allocator reuse before GPU completion.
        const UINT64 submitted = ++fenceValue_;
        Check(queue_->Signal(fence_.Get(), submitted), L"Signal");
        if (fence_->GetCompletedValue() < submitted) {
            Check(fence_->SetEventOnCompletion(submitted, fenceEvent_), L"SetEventOnCompletion");
            WaitForSingleObjectEx(fenceEvent_, INFINITE, FALSE);
        }
        ++frame_;
        if (frame_ == 1 || frame_ % 120 == 0) {
            char message[100] = {};
            sprintf_s(message, "Presented frame %llu", static_cast<unsigned long long>(frame_));
            Log(message);
        }
    }

private:
    void InitTexture() {
        // 48*4=192 source bytes/row; D3D12 copy pitch is 256.
        constexpr uint32_t size = 48;
        std::array<uint8_t, size * size * 4> pixels = {};
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const bool dark = ((x / 8) ^ (y / 8)) & 1;
                const size_t p = (y * size + x) * 4;
                pixels[p + 0] = dark ? 24 : 255;
                pixels[p + 1] = dark ? 108 : 228;
                pixels[p + 2] = dark ? 244 : 76;
                pixels[p + 3] = 255;
            }
        }
        // Input is packed into GX2 1D thin micro-tiles with an independently
        // specified 32bpp tile lookup. The decoder must restore normal rows.
        constexpr std::array<uint8_t, 64> tiledAddress = {
             0, 1, 2, 3, 8, 9,10,11,
             4, 5, 6, 7,12,13,14,15,
            16,17,18,19,24,25,26,27,
            20,21,22,23,28,29,30,31,
            32,33,34,35,40,41,42,43,
            36,37,38,39,44,45,46,47,
            48,49,50,51,56,57,58,59,
            52,53,54,55,60,61,62,63
        };
        std::array<uint8_t, size * size * 4> gx2Tiled = {};
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const size_t tile = (y / 8) * (size / 8) + x / 8;
                const size_t index = tiledAddress[(y % 8) * 8 + x % 8];
                std::memcpy(gx2Tiled.data() + (tile * 64 + index) * 4,
                            pixels.data() + (static_cast<size_t>(y) * size + x) * 4, 4);
            }
        }
        std::vector<uint8_t> untiled;
        if (!gfxd3d12::detile_rgba8(
                gx2Tiled.data(), gx2Tiled.size(), size, size, size,
                gfxd3d12::kGx2Tiled1DThin1, untiled) ||
            untiled.size() != pixels.size() ||
            std::memcmp(untiled.data(), pixels.data(), pixels.size()) != 0)
            throw std::runtime_error("GX2 1D thin1 texture roundtrip FAILED");
        Log("GX2 detile 1D thin1 48x48 RGBA8 roundtrip PASSED");
        // Reference 2D macro-tile fixture generated from Cemu's GX2 layout.
        // 48x48 visible texels, pitch 64 texels, padded to 48 rows.
        // Swizzle 0x500 selects pipe 1 and bank 2; bank/pipe offsets vary
        // between 32x16 macro-tile rows, not merely within a micro-tile.
        constexpr uint32_t macroPitch = 64;
        constexpr uint32_t macroSwizzle = 0x500;
        constexpr uint16_t macroBases[3][2][4] = {
            {{1280,1536,256,512}, {1024,1792,0,768}},
            {{256,512,1280,1536}, {0,768,1024,1792}},
            {{1792,1024,768,0}, {1536,1280,512,256}}
        };
        std::vector<uint8_t> macroTiled(macroPitch * size * 4, 0xA5);
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const size_t macroTile = size_t(y / 16) * (macroPitch / 32) + (x / 32);
                const size_t base = macroBases[y / 16][(y % 16) / 8][(x % 32) / 8];
                const size_t micro = tiledAddress[(y % 8) * 8 + (x % 8)] * 4;
                const size_t sourceAddress = macroTile * 2048 + base + micro;
                if (sourceAddress + 4 > macroTiled.size())
                    throw std::runtime_error("GX2 macro fixture allocation error");
                std::memcpy(macroTiled.data() + sourceAddress,
                            pixels.data() + (size_t(y) * size + x) * 4, 4);
            }
        }
        std::vector<uint8_t> macroDecoded;
        if (!gfxd3d12::detile_macro2d_thin1_rgba8(
                macroTiled.data(), macroTiled.size(), size, size,
                macroPitch, macroSwizzle, macroDecoded) ||
            macroDecoded.size() != pixels.size() ||
            std::memcmp(macroDecoded.data(), pixels.data(), pixels.size()) != 0)
            throw std::runtime_error("GX2 2D macro thin1 texture roundtrip FAILED");
        Log("GX2 macro 2D thin1 48x48 RGBA8 swizzle 0x500 roundtrip PASSED; uploading decoded macro texture");
        Check(allocator_->Reset(), L"Upload allocator Reset");
        Check(list_->Reset(allocator_.Get(), nullptr), L"Upload command list Reset");
        gfxd3d12::TextureUpload upload;
        Check(gfxd3d12::upload_linear_rgba8(
                  device_.Get(), list_.Get(), macroDecoded.data(), size, size, upload),
              L"Upload RGBA8 checker");
        Check(list_->Close(), L"Upload command list Close");
        ID3D12CommandList* commands[] = {list_.Get()};
        queue_->ExecuteCommandLists(1, commands);
        const UINT64 submitted = ++fenceValue_;
        Check(queue_->Signal(fence_.Get(), submitted), L"Texture upload Signal");
        if (fence_->GetCompletedValue() < submitted) {
            Check(fence_->SetEventOnCompletion(submitted, fenceEvent_),
                  L"Texture upload SetEventOnCompletion");
            if (WaitForSingleObjectEx(fenceEvent_, INFINITE, FALSE) != WAIT_OBJECT_0)
                throw std::runtime_error("Texture upload fence wait failed");
        }
        // The staging buffer stays valid until GPU fence completion.
        texture_ = upload.texture;
        D3D12_DESCRIPTOR_HEAP_DESC heap = {};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 1;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device_->CreateDescriptorHeap(&heap,
              IID_PPV_ARGS(srvHeap_.GetAddressOf())), L"Create SRV heap");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(texture_.Get(), &srv,
                                          srvHeap_->GetCPUDescriptorHandleForHeapStart());
        char message[200] = {};
        sprintf_s(message,
                  "RGBA8 checker texture upload COMPLETE: 48x48, source row=192, GPU row=%u, bytes=%llu, SRV ready",
                  upload.row_pitch, static_cast<unsigned long long>(upload.uploaded_bytes));
        Log(message);
    }

    void InitPipeline() {
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER parameter = {};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges = 1;
        parameter.DescriptorTable.pDescriptorRanges = &range;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC signature = {};
        signature.NumParameters = 1;
        signature.pParameters = &parameter;
        signature.NumStaticSamplers = 1;
        signature.pStaticSamplers = &sampler;
        signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> serialized, errors;
        Check(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1,
              serialized.GetAddressOf(), errors.GetAddressOf()), L"SerializeRootSignature");
        Check(device_->CreateRootSignature(0, serialized->GetBufferPointer(),
              serialized->GetBufferSize(), IID_PPV_ARGS(rootSignature_.GetAddressOf())),
              L"CreateRootSignature");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rootSignature_.Get();
        desc.VS = {gVertexShader, sizeof(gVertexShader)};
        desc.PS = {gPixelShader, sizeof(gPixelShader)};
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        Check(device_->CreateGraphicsPipelineState(&desc,
              IID_PPV_ARGS(pipeline_.GetAddressOf())), L"CreateGraphicsPipelineState");
    }
    ComPtr<ID3D12Resource> texture_;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipeline_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12Resource> buffers_[kBufferCount];
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    UINT64 fenceValue_ = 0;
    UINT64 frame_ = 0;
};

public ref class App sealed : IFrameworkView {
public:
    virtual void Initialize(CoreApplicationView^ applicationView) {
        applicationView->Activated +=
            ref new Windows::Foundation::TypedEventHandler<
                CoreApplicationView^,
                Windows::ApplicationModel::Activation::IActivatedEventArgs^>(
                this, &App::OnActivated);
    }

    virtual void SetWindow(CoreWindow^ window) {
        window_ = window;
        window_->Closed +=
            ref new Windows::Foundation::TypedEventHandler<
                CoreWindow^, CoreWindowEventArgs^>(this, &App::OnClosed);
    }

    virtual void Load(Platform::String^ entryPoint) {
        (void)entryPoint;
    }

    virtual void Run() {
        // Preserve the lifecycle that was verified on Xbox in v0.1.4.
        // First pump WinRT activation, THEN initialize D3D12 and LocalState logging.
        window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        OpenLog();
        Log("NSMBU Xbox RPX Inspection Probe v0.6.2 started");
        Log("[boot/0.5] Phase 1: reserve fixed Wii U guest virtual address space");
        xboxguest::GuestVm guestVm;
        if (!guestVm.open()) {
            char line[150] = {};
            sprintf_s(line, "[boot/0.5] guest VM FIXED 4GiB reserve FAILED: Win32=%lu",
                      static_cast<unsigned long>(guestVm.last_error()));
            Log(line);
        } else {
            Log(guestVm.write_watch()
                ? "[boot/0.5] guest VM FIXED 4GiB reserve PASSED (write-watch enabled)"
                : "[boot/0.5] guest VM FIXED 4GiB reserve PASSED (no write-watch)");
            // Explicit sparse commits only, NOT 4 GiB of physical memory.
            // Separate guest regions exercise the future RPX loader pathway.
            const bool mem2 = guestVm.commit(0x10000000u, 4096);
            const bool foreground = guestVm.commit(0xE0000000u, 4096);
            if (mem2 && foreground) {
                unsigned char* first = guestVm.ptr(0x10000000u);
                unsigned char* second = guestVm.ptr(0xE0000000u);
                first[0] = 0x12; first[1] = 0x34;
                first[2] = 0x56; first[3] = 0x78;
                second[0] = 0xA5; second[1] = 0x5A;
                if (first[0] == 0x12 && first[3] == 0x78 &&
                    second[0] == 0xA5 && second[1] == 0x5A)
                    Log("[boot/0.5] guest VM two sparse 64KiB commits PASSED (MEM2/foreground)");
                else
                    Log("[boot/0.5] guest VM sparse read/write FAILED");
            } else {
                char line[150] = {};
                sprintf_s(line, "[boot/0.5] guest VM sparse commit FAILED: Win32=%lu",
                          static_cast<unsigned long>(guestVm.last_error()));
                Log(line);
            }
        }
        Log("[boot/0.5] Phase 2: load generated recomp function/import table");
        HeadlessBootReport report = {};
        const int bootStatus = nsmbu_headless_public_boot(&report);
        {
            char line[260] = {};
            sprintf_s(line,
                      "[boot/0.5] table: entry=%08X funcs=%u imports=%u; native guest r3=%u; host slot=%08X r3=%u; unknown rejected=%u",
                      report.entry_address, report.recompiled_function_count,
                      report.recompiled_import_count, report.guest_result,
                      report.host_address, report.host_result,
                      report.unknown_address_rejected);
            Log(line);
        }
        Log(bootStatus == 0
            ? "[boot/0.5] guest-address dispatch + synthetic HLE callback PASSED (42 -> 50)"
            : "[boot/0.5] guest-address dispatch / synthetic HLE callback FAILED");
        Log("[boot/0.5] Phase 3: preserve verified D3D12 graphics path");
        Log("[rpx/0.6] Phase 0: begin bounded local RPX/ELF inspection");
        xboxrpxprobe::run(guestVm, Log);
        Log("[rpx/0.6] Phase 3: continue graphics; no LOCAL game code executed");
        if (!gfxd3d12::format_mapping_self_test()) {
            Log("GX2->DXGI format mapping self-test FAILED");
        } else {
            Log("GX2->DXGI format mapping self-test PASSED (RGBA8, sRGB, BC1, BC5, D16, unsupported)");
        }
        try {
            if (!gfxd3d12::detile_rgba8_self_test()) {
                Log("GX2 detile CPU self-test FAILED");
                throw std::runtime_error("GX2 detile self-test failure");
            }
            Log("GX2 detile CPU self-test PASSED (linear aligned, 1D thin1, 13x9, bounds)");
            Log("Starting D3D12 backend");
            D3D12Smoke graphics(reinterpret_cast<IUnknown*>(window_));
            gfxd3d12::attach_probe_host({
                &graphics,
                [](void* context, const float rgba[4], bool drawQuad) {
                    static_cast<D3D12Smoke*>(context)->Draw(rgba, drawQuad);
                }
            });
            render::g_backend = &gfxd3d12::probe_backend();
            Log("render::Backend D3D12 probe attached; guest GX2 draw calls unsupported");
            bool oldA = false;
            bool warningLogged = false;
            while (!closed_) {
                window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
                if (closed_) break;
                bool buttonA = false;
                try {
                    auto pads = Windows::Gaming::Input::Gamepad::Gamepads;
                    if (pads->Size > 0) {
                        auto rd = pads->GetAt(0)->GetCurrentReading();
                        buttonA = (static_cast<unsigned>(rd.Buttons) &
                                   static_cast<unsigned>(Windows::Gaming::Input::GamepadButtons::A)) != 0;
                    }
                } catch (Platform::Exception^ error) {
                    if (!warningLogged) {
                        char msg[100] = {};
                        sprintf_s(msg, "Gamepad HRESULT=0x%08lX", static_cast<unsigned long>(error->HResult));
                        Log(msg);
                        warningLogged = true;
                    }
                }
                if (buttonA != oldA) {
                    Log(buttonA ? "Gamepad A PRESSED" : "Gamepad A RELEASED");
                    oldA = buttonA;
                }
                const uint64_t frame = gfxd3d12::probe_frame_count();
                const float pulse = buttonA ? 0.52f :
                    (((frame / 60) % 2) == 0 ? 0.15f : 0.30f);
                const float rgba[4] = {0.05f, pulse, 0.35f, 1.0f};
                render::clear_color(nullptr, gfxd3d12::kProbeColorTarget, rgba);
                render::draw(nullptr, gfxd3d12::kProbePrimitive, 6, 0, 0, 0, 1);
                render::swap();
                if (frame == 0)
                    Log("render::Backend dispatch first clear/draw/swap PASSED");
            }
            Log("Window closed normally");
        } catch (const std::exception& error) {
            Log("GPU initialization or rendering failed:");
            Log(error.what());
        } catch (Platform::Exception^ error) {
            char message[96] = {};
            sprintf_s(message, "WinRT HRESULT=0x%08lX", static_cast<unsigned long>(error->HResult));
            Log(message);
        } catch (...) {
            Log("Unknown native exception during GPU test");
        }
        render::g_backend = nullptr;
        gfxd3d12::detach_probe_host();
        // A failing GPU probe intentionally keeps the known-good WinRT view alive.
        // The last successful checkpoint is in LocalState/nsmbu_xbox_diag.txt.
        while (!closed_) {
            window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
        }
        if (gLogFile != INVALID_HANDLE_VALUE) {
            CloseHandle(gLogFile);
            gLogFile = INVALID_HANDLE_VALUE;
        }
    }

    virtual void Uninitialize() {}

private:
    void OnActivated(
        CoreApplicationView^ applicationView,
        Windows::ApplicationModel::Activation::IActivatedEventArgs^ args) {
        (void)applicationView;
        (void)args;
        CoreWindow::GetForCurrentThread()->Activate();
    }

    void OnClosed(CoreWindow^ window, CoreWindowEventArgs^ args) {
        (void)window;
        (void)args;
        closed_ = true;
    }

    bool closed_ = false;
    CoreWindow^ window_ = nullptr;
};

ref class SmokeViewSource sealed : IFrameworkViewSource {
public:
    virtual IFrameworkView^ CreateView() {
        return ref new App();
    }
};
} // namespace NSMBUXboxSmoke

[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^ args) {
    (void)args;
    CoreApplication::Run(ref new NSMBUXboxSmoke::SmokeViewSource());
    return 0;
}
