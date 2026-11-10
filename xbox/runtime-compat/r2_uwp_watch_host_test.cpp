// R2.2 desktop host behavior test for the SAME APP-family write-watch object
// linked into the UWP AppContainer PE. The fixed guest region is PUBLIC data.
// No Xbox deployment or private game material is involved.
#include "core/write_watch.h"
#include "ppc.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>

namespace {
constexpr uint32_t kStart = 0x10010000u;
constexpr SIZE_T kCommit = 0x10000u;
constexpr SIZE_T kReserve = static_cast<SIZE_T>(0x100000000ull);
int fail(int error, const char* why) {
    std::fprintf(stderr, "R2.2 write-watch FAIL %d: %s (Win32=%lu)\n",
                 error, why, static_cast<unsigned long>(GetLastError()));
    return error;
}
}

int main() {
    void* fixed = VirtualAlloc(PPC_MEM_BASE, kReserve,
                               MEM_RESERVE | MEM_WRITE_WATCH, PAGE_NOACCESS);
    if (fixed != PPC_MEM_BASE) {
        if (fixed) VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(1, "fixed virtual memory reservation");
    }
    void* address = PPC_MEM_BASE + kStart;
    void* slice = VirtualAlloc(address, kCommit, MEM_COMMIT, PAGE_READWRITE);
    if (slice != address) {
        VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(2, "sparse guest commit");
    }

    void* written[8]{};
    ULONG_PTR count = 8;
    DWORD page = 0;
    if (GetWriteWatch(0, slice, kCommit, written, &count, &page) != 0 ||
        count != 0 || page == 0) {
        VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(3, "direct Windows GetWriteWatch baseline");
    }

    if (!wwatch::init(PPC_MEM_BASE, kReserve) || !wwatch::active()) {
        VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(4, "UWP tracker initialization");
    }
    if (!wwatch::readable(kStart, (uint32_t)kCommit) ||
        wwatch::readable(kStart + 0x20000u, 4096u)) {
        VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(12, "VirtualQuery must distinguish committed from reserved pages");
    }
    uint64_t checkpoint = wwatch::arm(kStart, (uint32_t)kCommit);
    if (wwatch::written_since(kStart, (uint32_t)kCommit, checkpoint)) {
        VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(5, "pristine page incorrectly reported written");
    }

    static_cast<volatile uint8_t*>(slice)[32] = 0x5a;
    if (!wwatch::written_since(kStart, (uint32_t)kCommit, checkpoint) ||
        !wwatch::changed_since(kStart, (uint32_t)kCommit, checkpoint)) {
        VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(6, "real guest write not detected");
    }
    uint32_t pages[8]{};
    if (wwatch::collect_written_pages(kStart, (uint32_t)kCommit,
                                     checkpoint, pages, 8) != 1u || pages[0] != 0u) {
        VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(7, "written page enumeration");
    }

    // Historical writes remain dirty after arm: intentional conservative
    // fallback until overlapping generation tracking is implemented.
    uint64_t next = wwatch::arm(kStart, (uint32_t)kCommit);
    if (!wwatch::written_since(kStart, (uint32_t)kCommit, next)) {
        VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(8, "conservative historical dirtiness");
    }
    wwatch::hint(kStart + 8192u, 4u);
    uint64_t calls = 0, bytes = 0;
    wwatch::take_hint_stats(calls, bytes);
    if (calls != 1u || bytes != 4u ||
        !wwatch::changed_since(kStart + 8192u, 4u, next)) {
        VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(9, "sticky hint/dirty accounting");
    }
    uint64_t faults = 1, protectedPages = 1;
    wwatch::take_stats(faults, protectedPages);
    if (faults != 0 || protectedPages != 0) {
        VirtualFree(fixed, 0, MEM_RELEASE);
        return fail(10, "UWP tracker must not install guard-page faults");
    }
    if (!VirtualFree(fixed, 0, MEM_RELEASE)) return fail(11, "VM release");
    std::puts("PASS: APP-family GetWriteWatch captures sparse guest writes; conservative hints/dirty semantics");
    return 0;
}
