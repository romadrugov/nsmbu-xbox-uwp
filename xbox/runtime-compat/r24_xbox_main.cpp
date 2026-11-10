// R2.3 independent Xbox Series S public UWP activation probe.
// Blue = initial render. Green = all tests passed. Red = a test failed.
// Full per-step details are in LocalState/nsmbu_r24_diag.txt.
// No private games, Nintendo RPX, assets or game textures are bundled.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <memory>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <exception>
#include "core/xbox_guest_vm.h"

using Microsoft::WRL::ComPtr;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

extern "C" int nsmbu_r23_device_probe(bool writeWatchAvailable, void (*log)(const char*));
extern "C" int nsmbu_r24_private_probe(void (*log)(const char*));

namespace NSMBUR24XboxProbe {
HANDLE gLog = INVALID_HANDLE_VALUE;

void Log(const char* message) noexcept {
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
    if (gLog != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(gLog, message, static_cast<DWORD>(strlen(message)), &written, nullptr);
        WriteFile(gLog, "\r\n", 2, &written, nullptr);
        FlushFileBuffers(gLog);
    }
}
void OpenLog() noexcept {
    try {
        auto folder = Windows::Storage::ApplicationData::Current->LocalFolder;
        std::wstring file(folder->Path->Data());
        file += L"\\nsmbu_r24_diag.txt";
        gLog = CreateFile2(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           CREATE_ALWAYS, nullptr);
        if (gLog == INVALID_HANDLE_VALUE)
            Log("[r23] LocalState log unavailable; try Visual Studio debug output");
    } catch (...) {
        OutputDebugStringA("[r23] Cannot access LocalState diagnostics\n");
    }
}
void Check(HRESULT hr, const char* label) {
    if (SUCCEEDED(hr)) return;
    char reason[190]{};
    sprintf_s(reason, "%s: HRESULT=0x%08lX", label, static_cast<unsigned long>(hr));
    Log(reason);
    throw std::runtime_error(reason);
}

// Minimal verified D3D12/flip-model path borrowed from standalone v0.7.0
// smoke test, but deliberately WITHOUT shaders or private game rendering.
class SolidScreen {
public:
    explicit SolidScreen(IUnknown* coreWindow) {
        Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                IID_PPV_ARGS(device_.GetAddressOf())),
              "D3D12CreateDevice");
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue_.GetAddressOf())),
              "CreateCommandQueue");
        ComPtr<IDXGIFactory2> factory;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())), "CreateDXGIFactory1");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 1280;
        desc.Height = 720;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        ComPtr<IDXGISwapChain1> chain;
        Check(factory->CreateSwapChainForCoreWindow(queue_.Get(), coreWindow,
                                                    &desc, nullptr, chain.GetAddressOf()),
              "CreateSwapChainForCoreWindow");
        Check(chain.As(&swap_), "IDXGISwapChain3");
        D3D12_DESCRIPTOR_HEAP_DESC h{};
        h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        h.NumDescriptors = 2;
        Check(device_->CreateDescriptorHeap(&h, IID_PPV_ARGS(rtv_.GetAddressOf())),
              "CreateDescriptorHeap");
        auto handle = rtv_->GetCPUDescriptorHandleForHeapStart();
        const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT i = 0; i < 2; ++i) {
            Check(swap_->GetBuffer(i, IID_PPV_ARGS(buffer_[i].GetAddressOf())), "GetBuffer");
            device_->CreateRenderTargetView(buffer_[i].Get(), nullptr, handle);
            handle.ptr += stride;
        }
        Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(allocator_.GetAddressOf())),
              "CreateCommandAllocator");
        Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator_.Get(), nullptr,
                                         IID_PPV_ARGS(list_.GetAddressOf())),
              "CreateCommandList");
        Check(list_->Close(), "Initial Close");
        Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf())),
              "CreateFence");
        event_ = CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
        if (!event_) throw std::runtime_error("CreateEventEx failed");
    }
    ~SolidScreen() {
        if (event_) CloseHandle(event_);
    }
    void Paint(const float rgba[4]) {
        const UINT index = swap_->GetCurrentBackBufferIndex();
        Check(allocator_->Reset(), "Allocator Reset");
        Check(list_->Reset(allocator_.Get(), nullptr), "CommandList Reset");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffer_[index].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list_->ResourceBarrier(1, &barrier);
        auto handle = rtv_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        list_->ClearRenderTargetView(handle, rgba, 0, nullptr);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        list_->ResourceBarrier(1, &barrier);
        Check(list_->Close(), "CommandList Close");
        ID3D12CommandList* lists[]{list_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        Check(swap_->Present(1, 0), "Present");
        const UINT64 submitted = ++fenceValue_;
        Check(queue_->Signal(fence_.Get(), submitted), "Signal");
        if (fence_->GetCompletedValue() < submitted) {
            Check(fence_->SetEventOnCompletion(submitted, event_), "SetEventOnCompletion");
            const DWORD waited = WaitForSingleObjectEx(event_, 10000, FALSE);
            if (waited != WAIT_OBJECT_0) throw std::runtime_error("GPU fence timeout");
        }
    }
private:
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swap_;
    ComPtr<ID3D12DescriptorHeap> rtv_;
    ComPtr<ID3D12Resource> buffer_[2];
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE event_ = nullptr;
    UINT64 fenceValue_ = 0;
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
    virtual void Load(Platform::String^ entryPoint) { (void)entryPoint; }
    virtual void Run() {
        // Match the known-good v0.7.0 Xbox activation ordering.
        window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        OpenLog();
        Log("[r23] R2.4 private leaf Xbox activation probe started");
        std::unique_ptr<SolidScreen> screen;
        try {
            screen.reset(new SolidScreen(reinterpret_cast<IUnknown*>(window_)));
            const float blue[4] = {0.04f, 0.13f, 0.40f, 1.f};
            screen->Paint(blue);
            Log("[r23/0] PASS: WinRT activation and initial blue D3D12 frame");
        } catch (const std::exception& e) {
            Log("[r23/0] FAIL: GPU setup/present");
            Log(e.what());
        } catch (...) { Log("[r23/0] FAIL: GPU unknown exception"); }

        int status = 99;
        try {
            Log("[r23/boot] reserve 4GiB VIRTUAL (not physical) guest address range");
            xboxguest::GuestVm guestVm;
            if (!guestVm.open()) {
                char line[150]{};
                sprintf_s(line, "[r23/boot] reserve FAILED: Win32=%lu",
                          static_cast<unsigned long>(guestVm.last_error()));
                Log(line);
                status = 91;
            } else if (!guestVm.commit(0x10010000u, 0x10000u)) {
                char line[150]{};
                sprintf_s(line, "[r23/boot] sparse commit FAILED: Win32=%lu",
                          static_cast<unsigned long>(guestVm.last_error()));
                Log(line);
                status = 92;
            } else {
                Log("[r23/boot] PASS: fixed 4GiB reserve, single 64KiB commit");
                status = nsmbu_r23_device_probe(guestVm.write_watch(), Log);
                if (status == 0) {
                    Log("[r24] R2.3 prerequisites PASS; testing one bound PPC leaf");
                    status = nsmbu_r24_private_probe(Log);
                }
            }
            // Keep GuestVm in scope while HLE reads and writes PPC_MEM_BASE.
            Log(status == 0 ? "[r24] FINAL PASS" : "[r24] FINAL FAIL");
        } catch (const std::exception& e) {
            Log("[r23] exception during guest-memory / HLE test");
            Log(e.what());
            status = 93;
        } catch (...) {
            Log("[r23] unknown exception during guest-memory / HLE test");
            status = 94;
        }
        if (screen) {
            const float green[4] = {0.03f, 0.42f, 0.18f, 1.f};
            const float red[4] = {0.60f, 0.05f, 0.04f, 1.f};
            try {
                screen->Paint(status == 0 ? green : red);
                Log("[r24] final D3D12 result color presented");
            } catch (...) { Log("[r23] final D3D12 result screen failed"); }
        }
        // Leave the result visible and LocalState log accessible for Xbox
        // Device Portal diagnostics. No tight render loop or busy-wait.
        while (!closed_) {
            window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
        }
        if (gLog != INVALID_HANDLE_VALUE) {
            CloseHandle(gLog);
            gLog = INVALID_HANDLE_VALUE;
        }
    }
    virtual void Uninitialize() {}
private:
    void OnActivated(CoreApplicationView^ view,
                     Windows::ApplicationModel::Activation::IActivatedEventArgs^ args) {
        (void)view;
        (void)args;
        CoreWindow::GetForCurrentThread()->Activate();
    }
    void OnClosed(CoreWindow^ window, CoreWindowEventArgs^ args) {
        (void)window; (void)args; closed_ = true;
    }
    bool closed_ = false;
    CoreWindow^ window_ = nullptr;
};

ref class ViewSource sealed : IFrameworkViewSource {
public:
    virtual IFrameworkView^ CreateView() { return ref new App(); }
};
} // namespace NSMBUR24XboxProbe

[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^ args) {
    (void)args;
    CoreApplication::Run(ref new NSMBUR24XboxProbe::ViewSource());
    return 0;
}
