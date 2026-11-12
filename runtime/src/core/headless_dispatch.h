#pragma once
// Public-fixture headless runtime: a bounded subset of the real dispatcher.
// Does not initialize Wii U OS services or execute commercial game code.
#include <stdint.h>
#include "ppc.h"
#ifdef __cplusplus
extern "C" {
#endif

// Imports generated g_recomp_funcs / g_recomp_imports from recomp_table.h.
// Text is indexed by guest word address, like runtime/src/core/core.cpp.
// Diagnostics retained after init failure/reset; no original game source here.
typedef struct HeadlessDispatchDiagnostic {
    uint32_t failure;       // 0 success; 1 allocation/size; 2 table insertion
    uint32_t source;        // 0 infrastructure; 1 functions; 2 imports
    uint32_t index;         // first failing source entry
    uint32_t address;       // first failing guest address
    uint32_t fallback;      // entries outside dense text/slot/host ranges
    uint32_t null_targets;  // unresolved null function targets (not registered)
} HeadlessDispatchDiagnostic;
int headless_dispatch_init(void);
void headless_dispatch_diagnostic(HeadlessDispatchDiagnostic* result);
void headless_dispatch_reset(void);
PpcFunc headless_dispatch_lookup(uint32_t guest_address);
uint32_t headless_dispatch_register_host(PpcFunc fn);
#ifdef __cplusplus
}
#endif
