// Standalone Xbox/UWP D3D12 smoke test. No Wii U code or game assets.
// Render a changing clear color to confirm a real swap chain and GPU presentation.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <memory>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

namespace {
constexpr UINT kBufferCount = 2;

void Check(HRESULT hr, const wchar_t* operation) {
    if (FAILED(hr)) {
        OutputDebugStringW(L"NSMBU Xbox smoke: ");
        OutputDebugStringW(operation);
        OutputDebugStringW(L" FAILED\n");
        throw std::runtime_error("D3D12 smoke test failed; inspect debugger output");
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
        OutputDebugStringW(L"NSMBU Xbox smoke: D3D12 initialized\n");
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

ref class SmokeView sealed : IFrameworkView {
public:
    virtual void Initialize(CoreApplicationView^) {}
    virtual void SetWindow(CoreWindow^ window) {
        window_ = window;
        window_->Closed += ref new Windows::Foundation::TypedEventHandler<
            CoreWindow^, CoreWindowEventArgs^>(this, &SmokeView::OnClosed);
    }
    virtual void Load(Platform::String^) {}
    virtual void Run() {
        window_->Activate();
        graphics_ = std::make_unique<D3D12Smoke>(reinterpret_cast<IUnknown*>(window_));
        while (!closed_) {
            window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            graphics_->Draw();
        }
        graphics_.reset();
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
    virtual IFrameworkView^ CreateView() { return ref new SmokeView(); }
};
}  // namespace

[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^) {
    CoreApplication::Run(ref new SmokeViewSource());
    return 0;
}
