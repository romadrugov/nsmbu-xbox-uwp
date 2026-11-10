// PUBLIC synthetic stand-in for the private R2.4 2-instruction game leaf.
// Only used for non-private CI validation; NOT Nintendo-derived code.
#include "ppc.h"

void nsmbu_r24_private_function(Cpu* c) {
    PPC_ENTER(0x02001000u);
    c->r[3] = 42u;
}
