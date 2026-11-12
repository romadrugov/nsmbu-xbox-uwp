/*
 * Xbox boot-first diagnostic runtime (PUBLIC source, no Nintendo code).
 *
 * This is deliberately NOT an implementation of the Wii U scheduler/Cafe OS.
 * It connects the already validated native recomp-table dispatcher and keeps
 * unsupported guest behavior fail-closed: no fake successful OS returns, no
 * silently dropped preemption, and no "game booted" claim from a linked EXE.
 *
 * Existing upstream ABI, declarations and original PowerPC arithmetic stay
 * unchanged. Full production counterparts live in core/core.cpp, threads.cpp,
 * trace.cpp and the Cafe OS implementations. Replace these limited facilities
 * with those real services before enabling a boot/gameplay claim.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <windows.h>
#include "core/headless_dispatch.h"
#include "recomp_table.h"
#include "bootfirst_api.h"

/* PPC_ENTER reads these from the generated PowerPC-to-C source. Three
   Espresso cores, same storage type as the upstream scheduler. */
volatile int g_core_preempt[3] = {0, 0, 0};
int g_ppc_trace = 0;

/* Real bounded trace capture for diagnostics. Not a fake trace callback. */
enum { kTraceCapacity = 1 << 14 };
static uint32_t s_trace[kTraceCapacity];
static _Atomic(uint32_t) s_trace_count = 0;

static void fail_unsupported(const char* kind, uint32_t pc,
                             const char* detail) {
    char message[320];
    (void)snprintf(message, sizeof(message),
                   "[nsmbu/bootfirst] FATAL %s at %08X: %s\r\n",
                   kind, (unsigned)pc, detail ? detail : "");
    OutputDebugStringA(message);
    /* Unsupported execution must not return success to game code. */
    abort();
}

void ppc_trace_enter(uint32_t pc) {
    const uint32_t pos =
        atomic_fetch_add_explicit(&s_trace_count, 1u, memory_order_relaxed);
    s_trace[pos & (kTraceCapacity - 1)] = pc;
}

uint32_t nsmbu_bootfirst_trace_count(void) {
    return atomic_load_explicit(&s_trace_count, memory_order_relaxed);
}
uint32_t nsmbu_bootfirst_trace_at(uint32_t position) {
    return s_trace[position & (kTraceCapacity - 1)];
}

/* Used by core/headless_dispatch.c for unknown text/host destinations.
   This diagnostic stage cannot continue safely after an unknown PC. */
void ppc_unimplemented(Cpu* cpu, uint32_t pc, uint32_t insn) {
    char desc[64];
    (void)snprintf(desc, sizeof(desc), "unknown PPC instruction %08X",
                   (unsigned)insn);
    (void)cpu;
    fail_unsupported("unimplemented PPC", pc, desc);
}
void ppc_trap(Cpu* cpu, uint32_t pc) {
    (void)cpu;
    fail_unsupported("PPC trap", pc, "guest trap");
}

/* There is no scheduler on this stage. If the guest requests preemption,
   stop instead of dropping it and corrupting game state. */
void ppc_preempt(Cpu* cpu) {
    fail_unsupported("preemption", cpu ? cpu->pc : 0,
                     "Xbox scheduler not initialized");
}

/* Upstream desktop runtime returns 0 for some missing HLEs; deliberately
   fail here so no fake successful Cafe OS behavior can mask a boot problem. */
void hle_unimplemented(Cpu* cpu, const char* library, const char* name) {
    char detail[200];
    (void)snprintf(detail, sizeof(detail), "missing HLE %.70s.%.100s",
                   library ? library : "?", name ? name : "?");
    fail_unsupported("Cafe OS HLE", cpu ? cpu->pc : 0, detail);
}

/* Loads generated function+import tables into the proven native x64 indexed
   dispatcher; does NOT invoke the Nintendo game's entry point. */
int nsmbu_bootfirst_init_dispatch(void) {
    if (g_recomp_entry_point == 0 || g_recomp_func_count == 0)
        return 0;
    return headless_dispatch_init();
}
// R4 preflight: load *all* linked game entries into the native dispatcher and
// check that the real entry point resolves. Do not call the entry function.
// The diagnostic shell checks the fixed guest VM separately first.
int nsmbu_bootfirst_inspect_table(NSMBUBootTableStatus* report) {
    if (!report) return -1;
    report->entry_address = g_recomp_entry_point;
    report->functions = g_recomp_func_count;
    report->imports = g_recomp_import_count;
    report->entry_registered = 0;
    if (g_recomp_entry_point == 0 || g_recomp_func_count == 0 ||
        g_recomp_func_count > 200000u || g_recomp_import_count > 20000u)
        return -1;
    const int initialized = nsmbu_bootfirst_init_dispatch();
    HeadlessDispatchDiagnostic diagnostic = {0};
    headless_dispatch_diagnostic(&diagnostic);
    report->dispatch_failure = diagnostic.failure;
    report->dispatch_source = diagnostic.source;
    report->dispatch_index = diagnostic.index;
    report->dispatch_address = diagnostic.address;
    report->fallback_entries = diagnostic.fallback;
    report->null_targets = diagnostic.null_targets;
    if (!initialized)
        return -2;
    report->entry_registered =
        headless_dispatch_lookup(g_recomp_entry_point) != 0 ? 1u : 0u;
    return report->entry_registered ? 0 : -3;
}

void nsmbu_bootfirst_shutdown_dispatch(void) {
    headless_dispatch_reset();
}
