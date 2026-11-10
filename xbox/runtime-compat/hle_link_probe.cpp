// Public source-only ABI bridge between clang-cl compiled Cafe OS HLE
// and the MSVC/C++-CX Windows Store executable. No Nintendo-derived code.
// This calls two genuine entry points from upstream coreinit_mem.cpp.
#include "core/runtime.h"

extern "C" void imp_coreinit_OSIsAddressRangeDCValid(Cpu*);
extern "C" void imp_coreinit_MEMGetBaseHeapHandle(Cpu*);
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
