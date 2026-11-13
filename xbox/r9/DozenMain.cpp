// Independent UWP application, NOT an update to R7D. This is the graphics
// integration gate needed before wiring the full NSMBU Recomp Vulkan backend.
// Creates a real Dozen/Vulkan swapchain on the Xbox UI thread.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "DozenFrame.h"

using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

namespace NSMBUDozenProbe {
namespace {
HANDLE g_file = INVALID_HANDLE_VALUE;

void Log(const char* line) noexcept {
    OutputDebugStringA(line);
    OutputDebugStringA("\r\n");
    if (g_file != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        WriteFile(g_file, line, static_cast<DWORD>(strlen(line)), &n, nullptr);
        WriteFile(g_file, "\r\n", 2, &n, nullptr);
        FlushFileBuffers(g_file);
    }
}
void OpenLog() {
    auto folder = Windows::Storage::ApplicationData::Current->LocalFolder;
    std::wstring path(folder->Path->Data());
    path += L"\\nsmbu_r9_dozen_gpu.txt";
    g_file = CreateFile2(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                         CREATE_ALWAYS, nullptr);
    if (g_file == INVALID_HANDLE_VALUE)
        OutputDebugStringA("[R9] ERROR: cannot create LocalState log\r\n");
}
}
public ref class App sealed : IFrameworkView {
public:
    virtual void Initialize(CoreApplicationView^ view) {
        view->Activated += ref new Windows::Foundation::TypedEventHandler<
            CoreApplicationView^,
            Windows::ApplicationModel::Activation::IActivatedEventArgs^>(
            this, &App::OnActivated);
    }
    virtual void SetWindow(CoreWindow^ window) {
        window_ = window;
        window_->Closed += ref new Windows::Foundation::TypedEventHandler<
            CoreWindow^, CoreWindowEventArgs^>(this, &App::OnClosed);
    }
    virtual void Load(Platform::String^) {}
    virtual void Run() {
        window_->Dispatcher->ProcessEvents(
            CoreProcessEventsOption::ProcessAllIfPresent);
        try {
            OpenLog();
            Log("[R9] Separate Dozen UWP GPU integration START");
            Log("[R9] Private game code NOT loaded; NSMBU GX2 NOT invoked");
            const bool success = nsmbur9::RunDozenFrame(
                reinterpret_cast<IUnknown*>(window_), &Log);
            Log(success ? "[R9] GPU integration READY for full-runtime port"
                        : "[R9] GPU integration FAILED; inspect previous Vulkan error");
        } catch (Platform::Exception^ ex) {
            char buf[160] = {};
            sprintf_s(buf, "[R9] WinRT exception HResult=0x%08X", ex->HResult);
            Log(buf);
        } catch (const std::exception& ex) {
            Log("[R9] Native exception:"); Log(ex.what());
        } catch (...) {
            Log("[R9] Unknown native/WinRT exception");
        }
        Log("[R9] Visual output is synthetic; no Mario gameplay");
        while (!closed_) {
            window_->Dispatcher->ProcessEvents(
                CoreProcessEventsOption::ProcessOneAndAllPending);
        }
        if (g_file != INVALID_HANDLE_VALUE) {
            CloseHandle(g_file);
            g_file = INVALID_HANDLE_VALUE;
        }
    }
    virtual void Uninitialize() {}
private:
    void OnActivated(CoreApplicationView^,
        Windows::ApplicationModel::Activation::IActivatedEventArgs^) {
        CoreWindow::GetForCurrentThread()->Activate();
    }
    void OnClosed(CoreWindow^, CoreWindowEventArgs^) { closed_ = true; }
    bool closed_ = false;
    CoreWindow^ window_ = nullptr;
};
ref class Source sealed : IFrameworkViewSource {
public:
    virtual IFrameworkView^ CreateView() { return ref new App(); }
};
}
[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^) {
    CoreApplication::Run(ref new NSMBUDozenProbe::Source());
    return 0;
}
