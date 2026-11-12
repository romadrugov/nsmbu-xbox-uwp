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
} NSMBUBootTableStatus;

// Initializes a bounded native address table from the linked recompilation.
// Returns 0 iff the entry address points to a real registered native function.
// Negative errors mean invalid metadata (-1), allocation/table failure (-2),
// or unresolved guest entrypoint (-3). Caller must shut down when done.
int nsmbu_bootfirst_inspect_table(NSMBUBootTableStatus* report);
void nsmbu_bootfirst_shutdown_dispatch(void);
uint32_t nsmbu_bootfirst_trace_count(void);
#ifdef __cplusplus
}
#endif
