// Standalone Xbox/UWP D3D12 smoke test. No Wii U code or game assets.
// Render a changing clear color to confirm a real swap chain and GPU presentation.
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

using Microsoft::WRL::ComPtr;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

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


void Check(HRESULT hr, const wchar_t* operation) {
    char message[256] = {};
    char name[160] = {};
    WideCharToMultiByte(CP_UTF8, 0, operation, -1, name, sizeof(name), nullptr, nullptr);
    sprintf_s(message, "%s: 0x%08lX", name, static_cast<unsigned long>(hr));
    Log(message);
    if (FAILED(hr)) {
        throw std::runtime_error(message);
    }
}

class D3D12Smoke final {
public:
    explicit D3D12Smoke(IUnknown* coreWindow) {
        Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                IID_PPV_ARGS(device_.GetAddressOf())), L"D3D12CreateDevice");

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue_.GetAddressOf())),
              L"CreateCommandQueue");

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

        ComPtr<IDXGISwapChain1> chain;
        Check(factory->CreateSwapChainForCoreWindow(
                  queue_.Get(), coreWindow, &swapDesc, nullptr, chain.GetAddressOf()),
              L"CreateSwapChainForCoreWindow");
        Check(chain.As(&swapChain_), L"IDXGISwapChain3");

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
        Log("D3D12 initialized");
    }

    ~D3D12Smoke() {
        if (fenceEvent_) CloseHandle(fenceEvent_);
    }

    void Draw() {
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
        // Alternating shades confirm the GPU is continuously drawing, rather than showing a static splash.
        const float pulse = (frame_ / 60) % 2 == 0 ? 0.15f : 0.30f;
        const FLOAT clear[4] = {0.05f, pulse, 0.35f, 1.0f};
        list_->ClearRenderTargetView(handle, clear, 0, nullptr);

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
    virtual void Initialize(CoreApplicationView^ applicationView) { (void)applicationView; }
    virtual void SetWindow(CoreWindow^ window) {
        window_ = window;
        window_->Closed += ref new Windows::Foundation::TypedEventHandler<
            CoreWindow^, CoreWindowEventArgs^>(this, &App::OnClosed);
    }
    virtual void Load(Platform::String^ entryPoint) { (void)entryPoint; }
    virtual void Run() {
        OpenLog();
        Log("NSMBU Xbox smoke diagnostic v0.1.1 started");
        try {
            window_->Activate();
            Log("CoreWindow Activate completed");
            graphics_ = std::make_unique<D3D12Smoke>(reinterpret_cast<IUnknown*>(window_));
            Log("Graphics constructor completed");
            while (!closed_) {
                window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
                graphics_->Draw();
            }
            Log("Window closed normally");
        } catch (const std::exception& error) {
            Log("Caught native C++ exception:");
            Log(error.what());
        } catch (Platform::Exception^ error) {
            Log("Caught C++/CX exception");
            char message[96] = {};
            sprintf_s(message, "HResult=0x%08lX", static_cast<unsigned long>(error->HResult));
            Log(message);
        } catch (...) {
            Log("Caught unknown exception");
        }
        graphics_.reset();
        if (!closed_) {
            Log("Holding app open after failure; retrieve LocalState/nsmbu_xbox_diag.txt");
            while (!closed_) {
                window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
            }
        }
        if (gLogFile != INVALID_HANDLE_VALUE) {
            CloseHandle(gLogFile);
            gLogFile = INVALID_HANDLE_VALUE;
        }
    }
    virtual void Uninitialize() {}

private:
    void OnClosed(CoreWindow^, CoreWindowEventArgs^) { closed_ = true; }
    bool closed_ = false;
    CoreWindow^ window_ = nullptr;
    std::unique_ptr<D3D12Smoke> graphics_;
};

ref class SmokeViewSource sealed : IFrameworkViewSource {
public:
    virtual IFrameworkView^ CreateView() { return ref new App(); }
};
}  // namespace

[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^) {
    CoreApplication::Run(ref new NSMBUXboxSmoke::SmokeViewSource());
    return 0;
}
