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

static Cpu s_r5_cpu; // Prepared by R5; reused by owner-only R7.

#if defined(NSMBU_R7_ENTRY_PROBE)
// R7 has to be compiled into the private application explicitly. The current
// lightweight boot-first runtime has no Cafe OS/scheduler. Fail immediately
// at a missing service; never return invented successes to the game.
enum { kR7Exception = 0xE2470001u, kR7MaxFunctions = 20000u,
       kR7MaxLoopTicks = 500000u, kR7MaxElapsedMs = 2500u };
static _Atomic(int) s_r7_active = 0;
static _Atomic(uint32_t) s_r7_funcs = 0;
static _Atomic(uint32_t) s_r7_loops = 0;
static _Atomic(uint32_t) s_r7_last_pc = 0;
static uint64_t s_r7_start_ms = 0;
static char s_r7_reason[160];
// Owner-exclusive emulation of the *already created* desktop main OSThread.
// Initialized only after RPX SHA/CPU staging and guest memory commitment.
static uint32_t s_r7_main_thread_address = 0;
int nsmbu_r7_prepare_main_thread(uint32_t address,
                                 uint32_t low, uint32_t high,
                                 uint32_t game_entry) {
    // Use same 0x6A0-byte OSThread offsets/values as
    // runtime/src/core/threads.cpp::create_thread, not an arbitrary stub.
    if (address != 0x60100000u || (address & 7u) ||
        low != 0x60000000u || high != 0x60100000u ||
        game_entry != 0x02AA2E50u ||
        g_recomp_entry_point != game_entry ||
        s_r5_cpu.pc != game_entry ||
        s_r5_cpu.r[1] < low || s_r5_cpu.r[1] >= high)
        return -1;
    uint8_t* const thread = ppc_ptr(address);
    // Caller has committed & zeroed the complete 64-KiB guest memory page.
    memset(thread, 0, 0x6A0u);
    st32(address + 0x000u, 0x4F53436Fu); // OSContext magic
    st32(address + 0x004u, 0x6E747874u); // OSContext magic continuation
    st32(address + 0x304u, 2u);          // core affinity, Espresso core 1
    st8(address + 0x324u, 1u);           // osthread::READY at creation
    st8(address + 0x325u, 2u);           // thread attribute / affinity
    st16(address + 0x326u, 1u);          // first thread ID
    st32(address + 0x328u, 0u);          // not suspended
    st32(address + 0x32Cu, 16u);         // effective priority
    st32(address + 0x330u, 16u);         // base priority
    st32(address + 0x394u, high);        // stack base/top
    st32(address + 0x398u, low);         // stack end/bottom
    st32(address + 0x39Cu, game_entry);  // thread entrypoint
    s_r7_main_thread_address = address;
    return 0;
}

static void r7_abort(const char* reason) {
    if (reason && reason != s_r7_reason)
        (void)snprintf(s_r7_reason, sizeof(s_r7_reason), "%s", reason);
    // RaiseException is caught only by our owner-private entrypoint SEH frame.
    RaiseException(kR7Exception, EXCEPTION_NONCONTINUABLE, 0, NULL);
}
static void r7_budget_check(void) {
    if (atomic_load_explicit(&s_r7_funcs, memory_order_relaxed) >
            kR7MaxFunctions ||
        atomic_load_explicit(&s_r7_loops, memory_order_relaxed) >
            kR7MaxLoopTicks ||
        (GetTickCount64() - s_r7_start_ms) > kR7MaxElapsedMs) {
        r7_abort("instrumented native function/loop/time budget reached");
    }
}
// Only the R7-private rebuilt owner-derived C archive may reference this.
void nsmbu_r7_loop_tick(void) {
    if (!atomic_load_explicit(&s_r7_active, memory_order_relaxed)) return;
    atomic_fetch_add_explicit(&s_r7_loops, 1u, memory_order_relaxed);
    r7_budget_check();
}
#endif

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
#if defined(NSMBU_R7_ENTRY_PROBE)
    if (atomic_load_explicit(&s_r7_active, memory_order_relaxed)) {
        (void)snprintf(s_r7_reason, sizeof(s_r7_reason),
                       "%.56s at %08X: %.86s", kind, (unsigned)pc,
                       detail ? detail : "");
        atomic_store_explicit(&s_r7_last_pc, pc, memory_order_relaxed);
        r7_abort(s_r7_reason);
    }
#endif
    abort();
}

void ppc_trace_enter(uint32_t pc) {
    const uint32_t pos =
        atomic_fetch_add_explicit(&s_trace_count, 1u, memory_order_relaxed);
    s_trace[pos & (kTraceCapacity - 1)] = pc;
#if defined(NSMBU_R7_ENTRY_PROBE)
    if (atomic_load_explicit(&s_r7_active, memory_order_relaxed)) {
        atomic_store_explicit(&s_r7_last_pc, pc, memory_order_relaxed);
        atomic_fetch_add_explicit(&s_r7_funcs, 1u, memory_order_relaxed);
        r7_budget_check();
    }
#endif
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
#if defined(NSMBU_R7_ENTRY_PROBE)
    if (atomic_load_explicit(&s_r7_active, memory_order_acquire) &&
        cpu && library && name && strcmp(library, "coreinit") == 0) {
        if (strcmp(name, "OSGetCurrentThread") == 0 &&
            s_r7_main_thread_address != 0u) {
            // Real guest OSThread, not nullptr/pretend success.
            cpu->r[3] = s_r7_main_thread_address;
            return;
        }
        if (strcmp(name, "OSGetCoreId") == 0) {
            cpu->r[3] = cpu->core; // matches threads.cpp
            return;
        }
        if (strcmp(name, "OSMemoryBarrier") == 0) {
            atomic_thread_fence(memory_order_seq_cst);
            return;
        }
    }
#endif
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

// R5B CPU register snapshot, also used by owner-only R7 bootstrap.
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

// R7: first actual program entrypoint attempt, with all unsupported Cafe OS
// paths explicitly intercepted. This is NOT a complete game runtime. The
// public synthetic fixture links this code but is refused at runtime.
#ifdef NSMBU_R7_ENTRY_PROBE
static int r7_filter(unsigned int code,
                     struct _EXCEPTION_POINTERS* exception,
                     NSMBUR7EntryReport* output) {
    output->seh_code = code;
    output->exception_address = exception && exception->ExceptionRecord
        ? (uint64_t)(uintptr_t)exception->ExceptionRecord->ExceptionAddress
        : 0u;
    output->status = code == kR7Exception ? 2u : 3u;
    return EXCEPTION_EXECUTE_HANDLER;
}

static int r7_call_protected(PpcFunc fn, Cpu* cpu,
                             NSMBUR7EntryReport* output) {
    int returned = 0;
    // Deliberately in a plain C function: SEH must not unwind C++/CX objects.
    // Fail closed for access violations and unsupported services.
    __try {
        fn(cpu);
        returned = 1;
    } __except(r7_filter(GetExceptionCode(), GetExceptionInformation(),
                         output)) {
        returned = 0;
    }
    return returned;
}

int nsmbu_r7_try_entry(NSMBUR7EntryReport* report) {
    if (!report) return -1;
    memset(report, 0, sizeof(*report));
    report->entry = g_recomp_entry_point;
    // No guest PC can be passed from user input. The RPX checksum is
    // independently verified by RpxProbe before it calls this routine.
    if (s_r7_main_thread_address != 0x60100000u) return -4;
    if (g_recomp_entry_point != 0x02AA2E50u ||
        g_recomp_func_count != 84231u ||
        g_recomp_import_count != 2821u ||
        s_r5_cpu.pc != g_recomp_entry_point ||
        s_r5_cpu.core != 1u || !s_r5_cpu.r[1]) return -2;
    PpcFunc fn = headless_dispatch_lookup(g_recomp_entry_point);
    if (!fn) return -3;

    Cpu cpu = s_r5_cpu;
    cpu.pc = g_recomp_entry_point;
    cpu.lr = 0u;  // no synthetic OS caller or invented return address
    cpu.r[3] = 0; // argc=0, argv=null as in R5 CPU preflight
    cpu.r[4] = 0;
    s_r7_reason[0] = 0;
    atomic_store_explicit(&s_r7_funcs, 0, memory_order_relaxed);
    atomic_store_explicit(&s_r7_loops, 0, memory_order_relaxed);
    atomic_store_explicit(&s_r7_last_pc, cpu.pc, memory_order_relaxed);
    s_r7_start_ms = GetTickCount64();
    g_ppc_trace = 1;
    atomic_store_explicit(&s_r7_active, 1, memory_order_release);
    const int returned = r7_call_protected(fn, &cpu, report);
    atomic_store_explicit(&s_r7_active, 0, memory_order_release);
    g_ppc_trace = 0;
    report->functions_entered =
        atomic_load_explicit(&s_r7_funcs, memory_order_relaxed);
    report->loop_ticks =
        atomic_load_explicit(&s_r7_loops, memory_order_relaxed);
    report->last_pc =
        atomic_load_explicit(&s_r7_last_pc, memory_order_relaxed);
    report->last_lr = cpu.lr;
    report->final_r3 = cpu.r[3];
    if (returned) {
        report->status = 1u; // entrypoint returned (NOT gameplay)
        (void)snprintf(report->stop_reason, sizeof(report->stop_reason),
                       "real entrypoint returned (not a game boot)");
    } else {
        (void)snprintf(report->stop_reason, sizeof(report->stop_reason),
                       "%s", s_r7_reason[0] ? s_r7_reason : "SEH guest/host fault");
    }
    return 0;
}
#endif

// R6A: private-only opt-in. Invoke exactly one independently recognized
// two-opcode or one-opcode PPC leaf using the already-linked *actual* native
// recompilation. The caller must have verified the full USA_V0 RPX SHA,
// staged the .text bytes, and prepared the R5 CPU before calling.
//
// No guest entrypoint, data imports, HLE, schedulers or general PPC execution.
// This is intentionally narrow: a single unconditionally returning function.
// If the owner RPX has no such linked function, return NO_CANDIDATE.
// An unknown instruction, branch, memory access or extra opcode is NEVER
// admitted as a shortcut to a "game is running" claim.
#ifdef NSMBU_R6A_EXECUTE_LEAF
static uint32_t r6_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static int r6_candidate(const uint8_t* text, uint32_t base,
                        uint32_t length, uint32_t address, uint32_t pattern,
                        uint32_t* expected) {
    if (address < base || (address & 3u)) return 0;
    const uint64_t offset = (uint64_t)address - base;
    const uint64_t needed = (pattern == 2u) ? 8u : 4u;
    if (offset + needed > length) return 0;
    const uint32_t a = r6_be32(text + offset);
    if (pattern == 2u) {
        if ((a & 0xFFFF0000u) != 0x38600000u ||
            r6_be32(text + offset + 4u) != 0x4E800020u) return 0;
        *expected = (uint32_t)(int32_t)(int16_t)(a & 0xFFFFu);
        return 1;
    }
    if (pattern == 1u && a == 0x4E800020u) {
        *expected = 0x13572468u;  // unchanged r3 sentinel
        return 1;
    }
    return 0;
}
int nsmbu_r6_run_pure_leaf(const uint8_t* text, uint32_t text_address,
                           uint32_t text_size, NSMBUR6LeafReport* output) {
    if (!output) return -1;
    memset(output, 0, sizeof(*output));
    // Fail closed unless this is the known owner-private USA_V0 build.
    // In particular, the synthetic public fixture must not reach this call.
    if (!text || text_address != 0x02000020u ||
        text_size < 8u || text_size > 16u * 1024u * 1024u ||
        g_recomp_entry_point != 0x02AA2E50u ||
        g_recomp_func_count != 84231u ||
        g_recomp_import_count != 2821u ||
        s_r5_cpu.pc != g_recomp_entry_point ||
        s_r5_cpu.r[1] == 0u) return -2;

    // Prefer an observable constant return. Fall back to a real no-op leaf
    // only when the owner build has no matching 'li r3,imm; blr' function.
    for (uint32_t pattern = 2u; pattern >= 1u; --pattern) {
        for (uint32_t i = 0; i < g_recomp_func_count; ++i) {
            ++output->scanned_functions;
            const RecompEntry* entry = &g_recomp_funcs[i];
            if (!entry->fn ||
                headless_dispatch_lookup(entry->addr) != entry->fn)
                continue;
            uint32_t expected = 0;
            if (!r6_candidate(text, text_address, text_size, entry->addr,
                              pattern, &expected)) continue;

            Cpu cpu = s_r5_cpu;
            cpu.pc = entry->addr;
            cpu.r[3] = 0x13572468u;
            cpu.lr = 0u;
            output->candidate = entry->addr;
            output->pattern = pattern;
            output->initial_r3 = cpu.r[3];
            output->expected_r3 = expected;
            const uint32_t original_stack = cpu.r[1];
            const uint32_t original_r2 = cpu.r[2];
            const uint32_t original_r13 = cpu.r[13];

            // This is the first actually executed owner-derived x64 function.
            // The opcode check prohibits memory/HLE/branching from this leaf.
            entry->fn(&cpu);
            output->actual_r3 = cpu.r[3];
            output->actual_pc = cpu.pc;
            output->stack_preserved =
                cpu.r[1] == original_stack &&
                cpu.r[2] == original_r2 &&
                cpu.r[13] == original_r13;
            return (cpu.r[3] == expected && output->stack_preserved)
                ? 0 : -3;
        }
    }
    return 1;  // No strictly vetted leaf in verified native function table.
}
#endif

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
