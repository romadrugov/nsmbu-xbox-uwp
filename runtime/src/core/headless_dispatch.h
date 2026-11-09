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
int headless_dispatch_init(void);
void headless_dispatch_reset(void);
PpcFunc headless_dispatch_lookup(uint32_t guest_address);
uint32_t headless_dispatch_register_host(PpcFunc fn);
#ifdef __cplusplus
}
#endif
