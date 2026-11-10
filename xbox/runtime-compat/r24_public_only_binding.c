// Public-only fixture standing in for a user's private game function.
// Used in CI to prove R2.4 registration, call and CPU comparison wiring.
// Never represent this as authentic NSMBU code.
#include "ppc.h"

extern void nsmbu_r24_private_function(Cpu*);
PpcFunc g_r24_private_function = nsmbu_r24_private_function;
const uint32_t g_r24_guest_address = 0x02001000u;
const uint32_t g_r24_expected_r3 = 42u;
const uint32_t g_r24_expected_r4 = 0x2468ACE0u;
const uint32_t g_r24_expected_lr = 0u;
