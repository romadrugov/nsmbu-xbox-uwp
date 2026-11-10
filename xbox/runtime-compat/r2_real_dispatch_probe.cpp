// R2 diagnostic only: force the linker to resolve the actual production
// HLE registry and host dispatch from runtime/src/core/core.cpp.
// No game data, no fabricated HLE implementation, no Xbox packaging.
#include "core/runtime.h"
#include <cstdio>

extern "C" void imp_coreinit_OSIsAddressRangeDCValid(Cpu*);

int main() {
    // This lookup must come from core.cpp, not the old hle_link_host.cpp shim.
    const PpcFunc hle = hle_find("coreinit.rpl", "OSIsAddressRangeDCValid");
    if (!hle || hle != imp_coreinit_OSIsAddressRangeDCValid) {
        std::fputs("R2: real HLE registry lookup failed\n", stderr);
        return 1;
    }

    // This path must call production dispatch::register_host and lookup.
    const uint32_t address = dispatch::register_host(hle, "coreinit.OSIsAddressRangeDCValid");
    if (!address || dispatch::lookup(address) != hle) {
        std::fputs("R2: real HLE host dispatch failed\n", stderr);
        return 2;
    }
    Cpu cpu{};
    cpu.pc = address;
    cpu.r[3] = 0;
    ppc_dispatch(&cpu);
    if (cpu.r[3] != 1u) {
        std::fputs("R2: dispatched Cafe OS HLE returned unexpected value\n", stderr);
        return 3;
    }

    std::puts("PASS: production HleReg registry -> real dispatch -> upstream Cafe OS HLE");
    return 0;
}
