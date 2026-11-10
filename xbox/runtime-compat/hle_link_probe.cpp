// Public source-only ABI bridge between clang-cl compiled Cafe OS HLE
// and the MSVC/C++-CX Windows Store executable. No Nintendo-derived code.
// This calls two genuine entry points from upstream coreinit_mem.cpp.
#include "core/runtime.h"

extern "C" void imp_coreinit_OSIsAddressRangeDCValid(Cpu*);
extern "C" void imp_coreinit_MEMGetBaseHeapHandle(Cpu*);
extern "C" void imp_coreinit_MEMCreateExpHeapEx(Cpu*);
extern "C" void imp_coreinit_MEMAllocFromExpHeapEx(Cpu*);
extern "C" void imp_coreinit_MEMFreeToExpHeap(Cpu*);
extern "C" void imp_coreinit_MEMGetTotalFreeSizeForExpHeap(Cpu*);
extern "C" void imp_coreinit_MEMDestroyExpHeap(Cpu*);

extern "C" int nsmbu_test_hle_registry_valid();

extern "C" int nsmbu_runtime_hle_link_probe() {
    Cpu c{};
    c.r[3] = 0;
    imp_coreinit_OSIsAddressRangeDCValid(&c);
    if (c.r[3] != 1) return 1; // upstream always reports valid
    c.r[3] = 0;
    imp_coreinit_MEMGetBaseHeapHandle(&c);
    if (c.r[3] != 0) return 2; // no heaps have been initialized
    if (!nsmbu_test_hle_registry_valid()) return 3;
    return 0;
}

// Desktop-only execution fixture. The caller maps 64 KiB at the exact
// PPC_MEM_BASE + guest_start address before invoking this. Uses real
// coreinit_mem.cpp heap functions, no synthetic guest implementation.
extern "C" int nsmbu_runtime_hle_heap_roundtrip(uint32_t guest_start) {
    Cpu c{};
    c.r[3] = guest_start;
    c.r[4] = 0x10000u;
    imp_coreinit_MEMCreateExpHeapEx(&c);
    if (c.r[3] != guest_start) return 11;
    if (ld32(guest_start) != 0x45585048u) return 12;
    c.r[3] = guest_start;
    c.r[4] = 0x100u;
    c.r[5] = 0x20u;
    imp_coreinit_MEMAllocFromExpHeapEx(&c);
    uint32_t block = c.r[3];
    if (block < guest_start + 0x40u || block + 0x100u > guest_start + 0x10000u || (block & 31)) return 13;
    c.r[3] = guest_start;
    c.r[4] = block;
    imp_coreinit_MEMFreeToExpHeap(&c);
    c.r[3] = guest_start;
    imp_coreinit_MEMGetTotalFreeSizeForExpHeap(&c);
    if (c.r[3] != 0x10000u - 0x40u) return 14;
    c.r[3] = guest_start;
    imp_coreinit_MEMDestroyExpHeap(&c);
    if (c.r[3] != guest_start) return 15;
    return 0;
}
