// R2.3 Xbox device behavior test: original Cafe OS HLE and production
// dispatcher with a sparse guest reservation owned by xbox_guest_vm.cpp.
// Only the PUBLIC three-instruction PowerPC ELF is recompiled for this test.
// No game binary, game assets, or Zelda/Nintendo-specific test data included.
#include "core/runtime.h"
#include "core/write_watch.h"
#include "recomp_table.h"
#include <cstdint>
#include <cstdio>

namespace {
constexpr uint32_t kGuestHeap = 0x10010000u;
constexpr uint32_t kHeapBytes = 0x00010000u;

void note(void (*log)(const char*), const char* msg) {
    if (log) log(msg);
}
int failure(void (*log)(const char*), int code, const char* why) {
    if (log) {
        char line[220]{};
        std::snprintf(line, sizeof(line), "[r23] FAIL %d: %s", code, why);
        log(line);
    }
    return code;
}
bool execute_hle(const char* name, Cpu& cpu) {
    PpcFunc fn = hle_find("coreinit.rpl", name);
    if (!fn) return false;
    const uint32_t address = dispatch::register_host(fn, name);
    if (address < mem::kHleFuncBase || dispatch::lookup(address) != fn)
        return false;
    cpu.pc = address;
    ppc_dispatch(&cpu);
    return true;
}
}

// Preconditions: caller successfully reserved the fixed 4 GiB address
// with GuestVm and committed exactly [kGuestHeap, kGuestHeap+64KiB).
// This function DOES NOT call mem::init(), which commits full 4GiB
// in desktop core.cpp and is not safe for Xbox UWP app RAM quotas.
extern "C" int nsmbu_r23_device_probe(bool writeWatchAvailable,
                                      void (*log)(const char*)) {
    note(log, "[r23/1] production generated dispatch table");
    if (g_recomp_entry_point != 0x02000000u || g_recomp_func_count == 0u)
        return failure(log, 11, "synthetic PowerPC recomp table missing");

    dispatch::init();
    if (!dispatch::lookup(g_recomp_entry_point))
        return failure(log, 12, "production dispatch::init failed");
    Cpu cpu{};
    cpu.core = 0;
    cpu.pc = g_recomp_entry_point;
    ppc_dispatch(&cpu);
    if (cpu.r[3] != 42u)
        return failure(log, 13, "public generated PowerPC entry did not return 42");
    note(log, "[r23/1] PASS: real generated PPC entry returned 42");

    note(log, "[r23/2] production HLE registry and dispatch");
    if (!hle_find("coreinit.rpl", "OSIsAddressRangeDCValid") ||
        !hle_find("coreinit", "MEMCreateExpHeapEx"))
        return failure(log, 21, "genuine coreinit HLE entries not registered");
    cpu = Cpu{};
    cpu.r[3] = 0;
    if (!execute_hle("OSIsAddressRangeDCValid", cpu) || cpu.r[3] != 1u)
        return failure(log, 22, "real coreinit HLE dispatch returned wrong value");
    note(log, "[r23/2] PASS: production HLE dispatch");

    note(log, "[r23/3] UWP-safe write-watch and sparse VM readability");
    if (!wwatch::init(PPC_MEM_BASE, 0x100000000ull) || !wwatch::active())
        return failure(log, 31, "UWP write-watch initialization failed");
    if (!wwatch::readable(kGuestHeap, kHeapBytes))
        return failure(log, 32, "committed 64-KiB guest heap not readable");
    if (wwatch::readable(kGuestHeap + 0x00020000u, 4096u))
        return failure(log, 33, "reserved-only guest pages reported readable");
    if (writeWatchAvailable)
        note(log, "[r23/3] MEM_WRITE_WATCH reservation is available");
    else
        note(log, "[r23/3] MEM_WRITE_WATCH unavailable; conservative dirty fallback");
    const uint64_t stamp = wwatch::arm(kGuestHeap, kHeapBytes);
    note(log, "[r23/3] PASS: committed/reserved guest-page guard");

    note(log, "[r23/4] Cafe OS 64-KiB exp heap, allocate/free and destroy");
    cpu = Cpu{};
    cpu.r[3] = kGuestHeap;
    cpu.r[4] = kHeapBytes;
    if (!execute_hle("MEMCreateExpHeapEx", cpu) ||
        cpu.r[3] != kGuestHeap || ld32(kGuestHeap) != 0x45585048u)
        return failure(log, 41, "real MEMCreateExpHeapEx and EXPH header");

    cpu = Cpu{};
    cpu.r[3] = kGuestHeap;
    cpu.r[4] = 256;
    cpu.r[5] = 32;
    if (!execute_hle("MEMAllocFromExpHeapEx", cpu))
        return failure(log, 42, "real MEMAllocFromExpHeapEx dispatch");
    const uint32_t block = cpu.r[3];
    if (block < kGuestHeap + 0x40u ||
        block + 256u > kGuestHeap + kHeapBytes || (block & 31u))
        return failure(log, 43, "real HLE heap allocation bounds/alignment");
    cpu = Cpu{};
    cpu.r[3] = kGuestHeap;
    cpu.r[4] = block;
    if (!execute_hle("MEMFreeToExpHeap", cpu))
        return failure(log, 44, "real MEMFreeToExpHeap dispatch");
    cpu = Cpu{};
    cpu.r[3] = kGuestHeap;
    if (!execute_hle("MEMGetTotalFreeSizeForExpHeap", cpu) ||
        cpu.r[3] != kHeapBytes - 0x40u)
        return failure(log, 45, "Cafe OS heap free accounting");
    cpu = Cpu{};
    cpu.r[3] = kGuestHeap;
    if (!execute_hle("MEMDestroyExpHeap", cpu) || cpu.r[3] != kGuestHeap)
        return failure(log, 46, "real MEMDestroyExpHeap dispatch");
    note(log, "[r23/4] PASS: real Cafe OS heap create/alloc/free/destroy");

    note(log, "[r23/5] conservative dirty-page detection");
    if (!wwatch::changed_since(kGuestHeap, kHeapBytes, stamp))
        return failure(log, 51, "heap writes not detected by UWP tracking");
    const uint32_t n = wwatch::collect_written_pages(kGuestHeap, kHeapBytes,
                                                    stamp, &cpu.r[0], 1);
    if (writeWatchAvailable && n != 0xffffffffu && n == 0u)
        return failure(log, 52, "write-watch reported zero pages after heap writes");
    if (!writeWatchAvailable)
        note(log, "[r23/5] conservative dirty mode (no OS write-watch)");
    else
        note(log, "[r23/5] PASS: watch-aware guest heap dirty-page detection");

    note(log, "[r23] PASS ALL: synthetic PPC + production dispatcher + real Cafe OS HLE + sparse guest memory");
    return 0;
}
