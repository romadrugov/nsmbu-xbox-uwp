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

// R5B diagnostic Cpu instance. Uses the exact ABI the generated PPC
// functions expect, but **does not** call any generated function.
static Cpu s_r5_cpu;
int nsmbu_r5_prepare_cpu(uint32_t entry, uint32_t stack_base,
                         uint32_t stack_size, uint32_t sda_base,
                         uint32_t sda2_base, NSMBUR5CpuSnapshot* output) {
    if (!output) return -1;
    memset(output, 0, sizeof(*output));
    if (!entry || entry != g_recomp_entry_point ||
        !headless_dispatch_lookup(entry)) return -2;
    if (stack_base != 0x60000000u || stack_size < 0x00100000u ||
        stack_size > 0x01000000u || (stack_size & 0xFFFFu) != 0u)
        return -3;
    if (sda_base < 0x10000000u || sda_base >= 0x50000000u ||
        sda2_base < 0x10000000u || sda2_base >= 0x50000000u ||
        ((sda_base | sda2_base) & 3u))
        return -4;
    const uint32_t top = stack_base + stack_size;
    memset(&s_r5_cpu, 0, sizeof(s_r5_cpu));
    s_r5_cpu.pc = entry;
    s_r5_cpu.core = 1;  // matches upstream threads::init_cpu
    s_r5_cpu.r[1] = (top - 0x20u) & ~15u;
    s_r5_cpu.r[2] = sda2_base;
    s_r5_cpu.r[13] = sda_base;
    s_r5_cpu.r[3] = 0;  // argc=0; real startup argv not prepared yet
    s_r5_cpu.r[4] = 0;  // argv=null
    s_r5_cpu.lr = 0;
    // Exact upstream threads.cpp::init_cpu floating/quantization defaults.
    s_r5_cpu.gqr[2] = 0x40004u;
    s_r5_cpu.gqr[3] = 0x50005u;
    s_r5_cpu.gqr[4] = 0x60006u;
    s_r5_cpu.gqr[5] = 0x70007u;
    s_r5_cpu.fpscr = 4u;
    output->entry = s_r5_cpu.pc;
    output->stack_base = stack_base;
    output->stack_size = stack_size;
    output->r1 = s_r5_cpu.r[1];
    output->r2 = s_r5_cpu.r[2];
    output->r13 = s_r5_cpu.r[13];
    output->lr = s_r5_cpu.lr;
    output->core = s_r5_cpu.core;
    output->argc = s_r5_cpu.r[3];
    output->argv = s_r5_cpu.r[4];
    output->fpscr = s_r5_cpu.fpscr;
    output->gqr2 = s_r5_cpu.gqr[2];
    output->gqr3 = s_r5_cpu.gqr[3];
    output->gqr4 = s_r5_cpu.gqr[4];
    output->gqr5 = s_r5_cpu.gqr[5];
    return 0;
}

// The recompiler generates weak default import wrappers that abort through
// hle_unimplemented. Therefore a registered pointer does NOT mean the OS
// service is implemented. This audit never invokes any imported function.
int nsmbu_r5_audit_imports(NSMBUR5ImportAudit* output) {
    if (!output) return -1;
    memset(output, 0, sizeof(*output));
    if (g_recomp_import_count > 20000u) return -2;
    output->total = g_recomp_import_count;
    for (uint32_t i = 0; i < g_recomp_import_count; ++i) {
        const RecompImport* imp = &g_recomp_imports[i];
        if (imp->is_func) {
            output->function_imports++;
            if (imp->fn) output->function_pointers++;
            if (imp->fn && headless_dispatch_lookup(imp->slot) == imp->fn)
                output->function_dispatch_entries++;
        } else {
            output->data_imports++;
            if (imp->addr >= 0xC1000000u && imp->addr < 0xC2000000u &&
                (imp->addr & 0xFFFu) == 0u) {
                output->data_virtual_slots++;
                if (!output->first_data_addr || imp->addr < output->first_data_addr)
                    output->first_data_addr = imp->addr;
            }
        }
    }
    return 0;
}

void nsmbu_bootfirst_shutdown_dispatch(void) {
    headless_dispatch_reset();
}
