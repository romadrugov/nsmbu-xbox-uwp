#pragma once
// Public C ABI between clang-cl PPC native runtime and MSVC WinRT boot shell.
// This API never executes guest code. Zero assets or Nintendo code in header.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct NSMBUBootTableStatus {
    uint32_t entry_address;
    uint32_t functions;
    uint32_t imports;
    uint32_t entry_registered;
    uint32_t dispatch_failure;      // 0 success; 1 allocation; 2 insertion
    uint32_t dispatch_source;       // 0 infrastructure; 1 funcs; 2 imports
    uint32_t dispatch_index;        // index of failed entry when applicable
    uint32_t dispatch_address;      // guest address of failed entry
    uint32_t fallback_entries;      // mappings outside dense address ranges
    uint32_t null_targets;          // skipped null function pointers
} NSMBUBootTableStatus;

// Initializes a bounded native address table from the linked recompilation.
// Returns 0 iff the entry address points to a real registered native function.
// Negative errors mean invalid metadata (-1), allocation/table failure (-2),
// or unresolved guest entrypoint (-3). Caller must shut down when done.
int nsmbu_bootfirst_inspect_table(NSMBUBootTableStatus* report);
// R5B: initialize and inspect a real Cpu struct, without ever executing
// the game. Stack guest pages must be committed by the caller beforehand.
typedef struct NSMBUR5CpuSnapshot {
    uint32_t entry;
    uint32_t stack_base;
    uint32_t stack_size;
    uint32_t r1;
    uint32_t r2;
    uint32_t r13;
    uint32_t lr;
    uint32_t core;
    uint32_t argc;
    uint32_t argv;
    uint32_t fpscr;
    uint32_t gqr2;
    uint32_t gqr3;
    uint32_t gqr4;
    uint32_t gqr5;
} NSMBUR5CpuSnapshot;
// Returns 0 if native entry, stack and SDA regs are prepared; never jumps
// into Nintendo code. Negative values denote unsafe or missing prerequisites.
int nsmbu_r5_prepare_cpu(uint32_t entry, uint32_t stack_base,
                         uint32_t stack_size, uint32_t sda_base,
                         uint32_t sda2_base, NSMBUR5CpuSnapshot* output);
// R5C: purely descriptive diagnostics. Non-null pointers include weak
// hle_unimplemented defaults and MUST NOT be considered implemented Cafe OS.
typedef struct NSMBUR5ImportAudit {
    uint32_t total;
    uint32_t function_imports;
    uint32_t function_pointers;
    uint32_t function_dispatch_entries;
    uint32_t data_imports;
    uint32_t data_virtual_slots;
    uint32_t first_data_addr;
} NSMBUR5ImportAudit;
int nsmbu_r5_audit_imports(NSMBUR5ImportAudit* output);
// R6A: opt-in, one-shot execution of a statically vetted PURE leaf from the
// locally SHA-verified USA_V0 RPX. Unlike R5, success actually calls a
// recompiled game function. Does not call the game entrypoint or Cafe OS.
// Only a tiny PPC opcode pattern (li r3, imm; blr OR blr) is admitted.
// Returns: 0 executed and matched, 1 no safe candidate, negative = failed.
typedef struct NSMBUR6LeafReport {
    uint32_t candidate;
    uint32_t pattern;           // 2 = li r3, signed-imm; blr; 1 = blr
    uint32_t scanned_functions;
    uint32_t initial_r3;
    uint32_t expected_r3;
    uint32_t actual_r3;
    uint32_t actual_pc;
    uint32_t stack_preserved;
} NSMBUR6LeafReport;
// Valid only after successful R5 CPU setup and private RPX SHA check.
// 'text' must be the verified and staged .text section, not arbitrary bytes.
// Caller opt-in and runtime compile flag are REQUIRED before invocation.
int nsmbu_r6_run_pure_leaf(const uint8_t* text, uint32_t text_address,
                           uint32_t text_size, NSMBUR6LeafReport* output);
void nsmbu_bootfirst_shutdown_dispatch(void);
uint32_t nsmbu_bootfirst_trace_count(void);
#ifdef __cplusplus
}
#endif
