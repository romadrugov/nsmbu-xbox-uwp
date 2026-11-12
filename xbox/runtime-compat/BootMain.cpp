// R4 boot-preflight Xbox UWP shell. Linkable with PUBLIC synthetic PPC or
// PRIVATE full recompilation. It NEVER calls a Nintendo game function.
// Runs on a real Xbox to verify WinRT activation, fixed 4 GiB guest VA,
// two sparse 64 KiB commits, and the entire native dispatch table's entry.
// No Cafe OS, RPX data, game resources, native game entrypoint, or GX2 yet.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <memory>
#include "core/xbox_guest_vm.h"
#include "bootfirst_api.h"
#include "RpxProbe.h"

using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

namespace NSMBUBootPreflight {
namespace {
HANDLE g_logFile = INVALID_HANDLE_VALUE;

void Log(const char* text) noexcept {
    OutputDebugStringA("[NSMBU/R5] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
    if (g_logFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_logFile, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
        WriteFile(g_logFile, "\r\n", 2, &written, nullptr);
        FlushFileBuffers(g_logFile);
    }
}

void OpenLog() noexcept {
    try {
        auto folder = Windows::Storage::ApplicationData::Current->LocalFolder;
        std::wstring path(folder->Path->Data());
        path += L"\\nsmbu_r5e_boot_sections.txt";
        g_logFile = CreateFile2(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                CREATE_ALWAYS, nullptr);
    } catch (...) {
        OutputDebugStringA("[NSMBU/R5] LocalState log unavailable\r\n");
    }
}

void LogStatus(const char* what, int result) {
    char line[190] = {};
    sprintf_s(line, "%s: result=%d", what, result);
    Log(line);
}

bool CheckSparseGuestMemory(xboxguest::GuestVm& vm) {
    if (!vm.open()) {
        LogStatus("fixed guest 4GiB reservation FAILED, Win32", int(vm.last_error()));
        return false;
    }
    Log(vm.write_watch()
        ? "fixed guest VA reserved at 0x200000000000, write-watch enabled"
        : "fixed guest VA reserved at 0x200000000000, write-watch disabled");
    // Keep the null guest page inaccessible. Two independent sparse regions
    // were selected from previously tested console allocations.
    if (!vm.commit(0x10000000u, 0x1000u) ||
        !vm.commit(0xE0000000u, 0x1000u)) {
        LogStatus("guest sparse commit FAILED, Win32", int(vm.last_error()));
        return false;
    }
    uint8_t* const mem2 = vm.ptr(0x10000000u);
    uint8_t* const foreground = vm.ptr(0xE0000000u);
    mem2[0] = 0x12; mem2[1] = 0x34; mem2[2] = 0x56; mem2[3] = 0x78;
    foreground[0] = 0xA5; foreground[1] = 0x5A;
    if (mem2[0] != 0x12 || mem2[3] != 0x78 ||
        foreground[0] != 0xA5 || foreground[1] != 0x5A) {
        Log("guest sparse page read/write FAILED");
        return false;
    }
    Log("guest sparse page read/write PASS (2 x 64KiB committed)");
    return true;
}

void RunPreflight() {
    Log("R4 native boot preflight START; game code execution DISABLED");
    xboxguest::GuestVm guest;
    if (!CheckSparseGuestMemory(guest)) {
        Log("R4 STOP: Wii U guest address space not ready");
        return;
    }
    NSMBUBootTableStatus table = {};
    const int result = nsmbu_bootfirst_inspect_table(&table);
    char line[220] = {};
    sprintf_s(line, "native dispatch: entry=%08X functions=%u imports=%u resolved=%u status=%d",
              table.entry_address, table.functions, table.imports,
              table.entry_registered, result);
    Log(line);
    sprintf_s(line, "dispatch details: failure=%u source=%u index=%u address=%08X fallback=%u null=%u",
              table.dispatch_failure, table.dispatch_source, table.dispatch_index,
              table.dispatch_address, table.fallback_entries, table.null_targets);
    Log(line);
    if (result != 0 || !table.entry_registered) {
        Log("R4 STOP: native entrypoint mapping unavailable");
    } else {
        Log("R4 PREFLIGHT PASS: guest VM + entire native function table + entrypoint mapping");
        NSMBUR5ImportAudit audit = {};
        const int auditResult = nsmbu_r5_audit_imports(&audit);
        sprintf_s(line, "R5C import audit: status=%d total=%u function=%u fn-pointers=%u "
                  "dispatch-mapped=%u data=%u data-virtual-slots=%u first-data=%08X",
                  auditResult, audit.total, audit.function_imports,
                  audit.function_pointers, audit.function_dispatch_entries,
                  audit.data_imports, audit.data_virtual_slots, audit.first_data_addr);
        Log(line);
        Log("R5C WARNING: native import pointers include generated WEAK "
            "hle_unimplemented wrappers; mapped does NOT mean Cafe OS implemented");
    }
    if (result == 0 && table.entry_registered) {
        Log("R5E PHASE: stage desktop boot sections, zero MEM2 and audit RPX/CPU/RELA");
        Log("R5 inputs: LocalState/red-pro2.rpx; not embedded in AppX or GitHub");
        xboxrpxprobe::run(guest, &Log, "USA_V0");
        Log("R5B NOTE: guest Cpu prepared only after verified RPX staging; no relocations or Cafe OS");
    } else {
        Log("R5 SKIP: native function table not ready");
    }
    nsmbu_bootfirst_shutdown_dispatch();
    Log("R5 END: native Mario game entry never called; runtime scheduler/HLE not initialized");
}
}

public ref class App sealed : IFrameworkView {
public:
    virtual void Initialize(CoreApplicationView^ applicationView) {
        applicationView->Activated += ref new Windows::Foundation::TypedEventHandler<
            CoreApplicationView^,
            Windows::ApplicationModel::Activation::IActivatedEventArgs^>(
            this, &App::OnActivated);
    }
    virtual void SetWindow(CoreWindow^ window) {
        window_ = window;
        window_->Closed += ref new Windows::Foundation::TypedEventHandler<
            CoreWindow^, CoreWindowEventArgs^>(this, &App::OnClosed);
    }
    virtual void Load(Platform::String^ entryPoint) { (void)entryPoint; }
    virtual void Run() {
        window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        OpenLog();
        try {
            RunPreflight();
        } catch (const std::exception& error) {
            Log("R4 native exception:");
            Log(error.what());
        } catch (Platform::Exception^ ex) {
            LogStatus("R4 WinRT exception", int(ex->HResult));
        } catch (...) {
            Log("R4 unknown native/WinRT exception");
        }
        // Deliberately stay visible for diagnostics; a black screen is not
        // gameplay. Read LocalState/nsmbu_r5e_boot_sections.txt for results.
        Log("R5E diagnostic shell is idle; close app to exit");
        while (!closed_) {
            window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
        }
        if (g_logFile != INVALID_HANDLE_VALUE) {
            CloseHandle(g_logFile);
            g_logFile = INVALID_HANDLE_VALUE;
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

ref class ViewSource sealed : IFrameworkViewSource {
public:
    virtual IFrameworkView^ CreateView() { return ref new App(); }
};
}  // namespace NSMBUBootPreflight

[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^) {
    CoreApplication::Run(ref new NSMBUBootPreflight::ViewSource());
    return 0;
}
