#include "gfx/d3d12/gx2_detile.h"
#include <cstdio>
int main() {
    if (!gfxd3d12::detile_rgba8_self_test()) {
        std::fputs("GX2 detile host self-test FAILED\n", stderr);
        return 1;
    }
    std::puts("GX2 detile host self-test PASSED (linear, 1D thin1, pitch, bounds)");
    return 0;
}
