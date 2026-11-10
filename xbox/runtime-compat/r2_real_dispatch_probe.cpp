// Public R2.1 Windows host test: production core registry and dispatcher,
// unchanged Cafe OS HLE, and recomp-generated three-instruction fixture.
// This is NOT an Xbox UWP activation, and no game code/assets are included.
#include "core/runtime.h"
#include "recomp_table.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>

namespace {
constexpr uint32_t kGuestHeap = 0x10010000u;
constexpr size_t kHeapSize = 0x10000u;

int fail(int rc, const char* reason) {
    std::fprintf(stderr, "R2.1 FAIL (%d): %s\n", rc, reason);
    return rc;
}

// Register HLE functions through the production dispatcher, not a test shim.
bool run_hle(const char* name, Cpu& cpu) {
    PpcFunc fn = hle_find("coreinit.rpl", name);
    if (!fn) return false;
    const uint32_t entry = dispatch::register_host(fn, name);
    if (entry < mem::kHleFuncBase || dispatch::lookup(entry) != fn) return false;
    cpu.pc = entry;
    ppc_dispatch(&cpu);
    return true;
}
}

int main() {
    // table.c and the translated guest body must come from the actual
    // public recomp/android_fixture.py pipeline (not fabricated game data).
    if (g_recomp_func_count == 0u || g_recomp_entry_point != 0x02000000u)
        return fail(1, "missing recomp-generated public fixture table");
    dispatch::init();
    if (!dispatch::lookup(g_recomp_entry_point))
        return fail(2, "production dispatcher did not install public guest function");

    Cpu guest{};
    guest.core = 0;
    guest.pc = g_recomp_entry_point;
    ppc_dispatch(&guest);
    if (guest.r[3] != 42u)
        return fail(3, "real generated PowerPC function did not return 42");

    PpcFunc valid = hle_find("coreinit", "OSIsAddressRangeDCValid");
    if (!valid || valid != hle_find("coreinit.rpl", "OSIsAddressRangeDCValid"))
        return fail(4, "production HLE registry lookup failed");
    Cpu cpu{};
    cpu.r[3] = 0;
    if (!run_hle("OSIsAddressRangeDCValid", cpu) || cpu.r[3] != 1u)
        return fail(5, "production dispatcher did not invoke real Cafe OS HLE");

    // Work with the exact fixed PPC address, but commit only 64 KiB.
    // Do not call core.cpp mem::init() here: it commits the full 4 GiB.
    void* address = PPC_MEM_BASE + kGuestHeap;
    void* mapped = VirtualAlloc(address, kHeapSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (mapped != address) {
        if (mapped) VirtualFree(mapped, 0, MEM_RELEASE);
        return fail(6, "could not map exact synthetic 64-KiB PPC guest heap");
    }

    int result = 0;
    Cpu heap{};
    heap.r[3] = kGuestHeap;
    heap.r[4] = static_cast<uint32_t>(kHeapSize);
    if (!run_hle("MEMCreateExpHeapEx", heap) || heap.r[3] != kGuestHeap ||
        ld32(kGuestHeap) != 0x45585048u) {
        result = fail(7, "real HLE heap creation / big-endian header");
    } else {
        heap = Cpu{};
        heap.r[3] = kGuestHeap;
        heap.r[4] = 256u;
        heap.r[5] = 32u;
        if (!run_hle("MEMAllocFromExpHeapEx", heap)) {
            result = fail(8, "HLE heap allocation dispatch");
        } else {
            const uint32_t block = heap.r[3];
            if (block < kGuestHeap + 0x40u || block + 256u > kGuestHeap + kHeapSize || (block & 31u)) {
                result = fail(9, "HLE allocation bounds / alignment");
            } else {
                heap = Cpu{};
                heap.r[3] = kGuestHeap;
                heap.r[4] = block;
                if (!run_hle("MEMFreeToExpHeap", heap)) {
                    result = fail(10, "HLE heap free dispatch");
                } else {
                    heap = Cpu{};
                    heap.r[3] = kGuestHeap;
                    if (!run_hle("MEMGetTotalFreeSizeForExpHeap", heap) ||
                        heap.r[3] != kHeapSize - 0x40u)
                        result = fail(11, "HLE free-size accounting");
                }
            }
        }
        heap = Cpu{};
        heap.r[3] = kGuestHeap;
        if (!run_hle("MEMDestroyExpHeap", heap) || heap.r[3] != kGuestHeap)
            result = fail(12, "HLE heap destroy dispatch");
    }
    if (!VirtualFree(mapped, 0, MEM_RELEASE))
        return fail(13, "guest heap release failed");
    if (result) return result;

    std::puts("PASS: generated guest code (42) -> production dispatcher -> actual Cafe OS HLE / heap lifecycle");
    return 0;
}
