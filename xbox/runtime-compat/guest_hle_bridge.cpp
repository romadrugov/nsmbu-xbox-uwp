// PUBLIC synthetic PPC -> recomp address table -> native dispatch ->
// genuine upstream Cafe OS memory HLE. Compiled by clang-cl (not MSVC).
// Nintendo game data and functions must NEVER be used in this test.
#include <cstdint>
#include "core/runtime.h"
#include "core/headless_dispatch.h"
#include "recomp_table.h"

extern "C" void imp_coreinit_OSIsAddressRangeDCValid(Cpu*);
extern "C" void imp_coreinit_MEMGetBaseHeapHandle(Cpu*);
extern "C" void imp_coreinit_MEMCreateExpHeapEx(Cpu*);
extern "C" void imp_coreinit_MEMAllocFromExpHeapEx(Cpu*);
extern "C" void imp_coreinit_MEMFreeToExpHeap(Cpu*);
extern "C" void imp_coreinit_MEMGetTotalFreeSizeForExpHeap(Cpu*);
extern "C" void imp_coreinit_MEMDestroyExpHeap(Cpu*);

namespace {
// The author-created fixture's headless dispatcher is intentionally a
// subset of the upstream core dispatcher; this is NOT production dispatch.
struct ScopedDispatch {
    bool ok;
    ScopedDispatch() : ok(headless_dispatch_init() != 0) {}
    ~ScopedDispatch() { headless_dispatch_reset(); }
    ScopedDispatch(const ScopedDispatch&) = delete;
    ScopedDispatch& operator=(const ScopedDispatch&) = delete;
};

bool call_host(Cpu* cpu, PpcFunc fn) {
    const uint32_t address = headless_dispatch_register_host(fn);
    if (!address || headless_dispatch_lookup(address) != fn) return false;
    cpu->pc = address;
    ppc_dispatch(cpu);
    return true;
}
}

// Can be linked into a UWP AppContainer x64 executable and executed on
// the Windows desktop host fixture. Never accesses guest memory.
extern "C" int nsmbu_public_guest_hle_link_probe() {
    ScopedDispatch dispatch;
    if (!dispatch.ok) return 1;
    if (g_recomp_entry_point != 0x02000000u || g_recomp_func_count != 1)
        return 2;
    if (!headless_dispatch_lookup(g_recomp_entry_point)) return 3;

    Cpu cpu{};
    cpu.core = 0;
    cpu.pc = g_recomp_entry_point;
    ppc_dispatch(&cpu); // actual recompiled three-instruction PPC -> 42
    if (cpu.r[3] != 42u) return 4;
    if (headless_dispatch_lookup(0xDEADC0DEu)) return 5;

    cpu.r[3] = 0;
    if (!call_host(&cpu, imp_coreinit_OSIsAddressRangeDCValid))
        return 6;
    if (cpu.r[3] != 1) return 7;

    cpu.r[3] = 0;
    if (!call_host(&cpu, imp_coreinit_MEMGetBaseHeapHandle))
        return 8;
    if (cpu.r[3] != 0) return 9; // before heap initialization
    return 0;
}

// Desktop behavioral test only. Caller must map 64 KiB at
// PPC_MEM_BASE + guest_start first; no unmapped memory may be accessed.
// Every Cafe OS HLE operation is reached via a dynamically registered
// guest host address and the same ppc_dispatch() lookup.
extern "C" int nsmbu_public_guest_hle_heap_roundtrip(uint32_t guest_start) {
    ScopedDispatch dispatch;
    if (!dispatch.ok) return 20;
    Cpu c{};
    c.core = 0;
    c.r[3] = guest_start;
    c.r[4] = 0x10000u;
    if (!call_host(&c, imp_coreinit_MEMCreateExpHeapEx)) return 21;
    if (c.r[3] != guest_start || ld32(guest_start) != 0x45585048u)
        return 22;

    c.r[3] = guest_start;
    c.r[4] = 0x100u;
    c.r[5] = 0x20u;
    if (!call_host(&c, imp_coreinit_MEMAllocFromExpHeapEx)) return 23;
    const uint32_t block = c.r[3];
    if (block < guest_start + 0x40u ||
        block + 0x100u > guest_start + 0x10000u ||
        (block & 0x1Fu)) return 24;

    c.r[3] = guest_start;
    c.r[4] = block;
    if (!call_host(&c, imp_coreinit_MEMFreeToExpHeap)) return 25;

    c.r[3] = guest_start;
    if (!call_host(&c, imp_coreinit_MEMGetTotalFreeSizeForExpHeap))
        return 26;
    if (c.r[3] != 0x10000u - 0x40u) return 27;

    c.r[3] = guest_start;
    if (!call_host(&c, imp_coreinit_MEMDestroyExpHeap)) return 28;
    if (c.r[3] != guest_start) return 29;
    return 0;
}
