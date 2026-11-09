// Host-side test of actual recompiler table -> guest CPU -> synthetic HLE.
#include "ppc_fixture_bridge.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    HeadlessBootReport report = {0};
    const int rc = nsmbu_headless_public_boot(&report);
    printf("headless public boot: rc=%d, entry=%08X, funcs=%u, imports=%u, "
           "guest_r3=%u, host_slot=%08X, host_r3=%u, unknown_rejected=%u\n",
           rc, report.entry_address, report.recompiled_function_count,
           report.recompiled_import_count, report.guest_result,
           report.host_address, report.host_result,
           report.unknown_address_rejected);
    assert(rc == 0);
    assert(report.entry_address == 0x02000000u);
    assert(report.recompiled_function_count >= 1u);
    assert(report.guest_result == 42u);
    assert(report.host_address == 0xC2000000u);
    assert(report.host_result == 50u);
    assert(report.unknown_address_rejected == 1u);
    puts("PUBLIC headless boot PASSED: recomp table -> dispatch -> native function -> registered host callback");
    return 0;
}
