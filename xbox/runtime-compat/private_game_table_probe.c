/*
 * PRIVATE-FIRST-LINK PROBE (public source, no Nintendo data).
 *
 * This translation unit is compiled by clang-cl and forces the linker to
 * resolve the genuine recomp-generated table.c and its function pointers.
 * It does not call a Nintendo function, initialize Cafe OS, or render frames.
 * Default constants identify the verified user's USA_V0 source inventory.
 * Public CI overrides them for its original 3-instruction fixture.
 */
#include "recomp_table.h"
#include <stdint.h>

#ifndef NSMBU_EXPECT_ENTRY
#define NSMBU_EXPECT_ENTRY 0x02AA2E50u
#endif
#ifndef NSMBU_EXPECT_FUNCS
#define NSMBU_EXPECT_FUNCS 84231u
#endif
#ifndef NSMBU_EXPECT_IMPORTS
#define NSMBU_EXPECT_IMPORTS 2821u
#endif

#ifdef __cplusplus
extern "C"
#endif
int nsmbu_private_game_link_metadata(void) {
    if (g_recomp_entry_point != (uint32_t)NSMBU_EXPECT_ENTRY) return 1;
    if (g_recomp_func_count != (unsigned)NSMBU_EXPECT_FUNCS) return 2;
    if (g_recomp_import_count != (unsigned)NSMBU_EXPECT_IMPORTS) return 3;
    if (!g_recomp_funcs || !g_recomp_funcs[0].fn) return 4;
    if (g_recomp_funcs[0].addr == 0) return 5;
    // Referencing the real table also pulls the recompiled function pointers
    // into the native link. No game function is executed in this probe.
    return 0;
}
