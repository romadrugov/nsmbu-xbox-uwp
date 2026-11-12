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
void nsmbu_bootfirst_shutdown_dispatch(void);
uint32_t nsmbu_bootfirst_trace_count(void);
#ifdef __cplusplus
}
#endif
