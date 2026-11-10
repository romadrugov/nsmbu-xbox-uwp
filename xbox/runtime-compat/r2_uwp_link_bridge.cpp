// AppContainer *link* probe. This is not activated or executed by CI.
// All referenced symbols come from actual upstream core.cpp + coreinit_mem.cpp,
// plus the public three-instruction recomp/android_fixture.py output.
#include "core/runtime.h"
#include "core/write_watch.h"
#include "recomp_table.h"

extern "C" int nsmbu_r2_uwp_link_probe() {
    if (g_recomp_entry_point != 0x02000000u || !g_recomp_func_count) return 1;
    dispatch::init();
    Cpu cpu{};
    cpu.pc = g_recomp_entry_point;
    ppc_dispatch(&cpu);
    if (cpu.r[3] != 42u) return 2;

    PpcFunc hle = hle_find("coreinit.rpl", "OSIsAddressRangeDCValid");
    if (!hle) return 3;
    uint32_t registered = dispatch::register_host(hle, "OSIsAddressRangeDCValid");
    if (dispatch::lookup(registered) != hle) return 4;
    cpu = Cpu{};
    cpu.pc = registered;
    ppc_dispatch(&cpu);
    if (cpu.r[3] != 1u) return 5;

    // Force nontrivial reference to the UWP watch implementation;
    // no runtime guest reservation/initialization is attempted here.
    const uint64_t seq = wwatch::write_seq();
    return seq ? 0 : 6;
}
