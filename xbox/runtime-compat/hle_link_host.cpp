// Test-only host support for linking unchanged upstream coreinit_mem.cpp.
// This deliberately does NOT replace the production scheduler, memory
// dispatcher, or write-watch implementation. Unsupported execution aborts
// rather than silently pretending the real runtime is initialized.
#include "core/runtime.h"
#include "core/write_watch.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
#include <windows.h>

bool g_trace_hle = false;

namespace {
std::vector<HleReg*>& registered_hle() {
    static std::vector<HleReg*> entries;
    return entries;
}
}

HleReg::HleReg(const char* l, const char* n, PpcFunc f)
    : lib(l), name(n), fn(f) {
    registered_hle().push_back(this);
}

extern "C" int nsmbu_test_hle_registry_valid() {
    bool found_valid = false;
    bool found_heap = false;
    for (const HleReg* h : registered_hle()) {
        if (!h || std::strcmp(h->lib, "coreinit") != 0) continue;
        if (std::strcmp(h->name, "OSIsAddressRangeDCValid") == 0 && h->fn) found_valid = true;
        if (std::strcmp(h->name, "MEMGetBaseHeapHandle") == 0 && h->fn) found_heap = true;
    }
    return found_valid && found_heap;
}

void log_msg(const char* fmt, ...) {
    char message[512]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

namespace dispatch {
uint32_t register_host(PpcFunc, const char*) {
    // Registration into the full runtime dispatch table is not linked in
    // this test executable. Do not allow this path to silently succeed.
    std::abort();
}
}

namespace wwatch {
void hint(uint32_t, uint32_t) {
    // The isolated link test cannot safely simulate guest write-watch.
    std::abort();
}
}
