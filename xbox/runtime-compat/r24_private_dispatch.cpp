// R2.4 bridge: a single locally compiled PowerPC game function may be
// installed at its canonical Wii U guest address AFTER public R2.3 init.
//
// The g_r24_* inputs are provided by one C file generated OUTSIDE the public
// checkout by tools/xbox/r24_leaf_select.py. This module is game-free and
// safe to build in public CI with a separately authored synthetic binding.
#include "core/runtime.h"
#include <cstdint>
#include <cstdio>

extern "C" {
extern PpcFunc g_r24_private_function;
extern const uint32_t g_r24_guest_address;
extern const uint32_t g_r24_expected_r3;
extern const uint32_t g_r24_expected_r4;
extern const uint32_t g_r24_expected_lr;
}

namespace {
int fail(void (*log)(const char*), int code, const char* why) {
    if (log) {
        char message[180]{};
        std::snprintf(message, sizeof(message), "[r24] FAIL %d: %s", code, why);
        log(message);
    }
    return code;
}
}

extern "C" int nsmbu_r24_private_probe(void (*log)(const char*)) {
    if (!g_r24_private_function)
        return fail(log, 71, "no private function binding");
    if (g_r24_guest_address < 0x02000000u ||
        g_r24_guest_address >= 0x03000000u ||
        (g_r24_guest_address & 3u))
        return fail(log, 72, "invalid guest function address");
    if (dispatch::lookup(g_r24_guest_address))
        return fail(log, 73, "selected guest address already registered");

    // R2.3 has already called production dispatch::init; this registers
    // exactly one additional function without replacing its public table.
    dispatch::set(g_r24_guest_address, g_r24_private_function);
    if (dispatch::lookup(g_r24_guest_address) != g_r24_private_function)
        return fail(log, 74, "production dispatch::set/lookup mismatch");

    Cpu cpu{};
    cpu.pc = g_r24_guest_address;
    cpu.core = 0;
    cpu.r[3] = 0x13579BDFu;
    cpu.r[4] = 0x2468ACE0u;
    cpu.r[5] = 0x10203040u;
    cpu.r[6] = 7u;
    cpu.r[7] = 3u;
    if (log) log("[r24] invoking selected PPC function via production ppc_dispatch");
    ppc_dispatch(&cpu);

    char message[180]{};
    std::snprintf(message, sizeof(message),
                  "[r24] result r3=%08X r4=%08X lr=%08X, expected=%08X/%08X/%08X",
                  cpu.r[3], cpu.r[4], cpu.lr,
                  g_r24_expected_r3, g_r24_expected_r4, g_r24_expected_lr);
    if (log) log(message);
    if (cpu.r[3] != g_r24_expected_r3 ||
        cpu.r[4] != g_r24_expected_r4 ||
        cpu.lr != g_r24_expected_lr)
        return fail(log, 75, "game CPU result differs from local reference");

    if (log) log("[r24] PASS: single selected recomp function ran via production dispatch");
    return 0;
}
