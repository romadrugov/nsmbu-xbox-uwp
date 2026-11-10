// Host-only R2.4 public wiring test (not Nintendo game code).
#include "core/runtime.h"
#include <cstdio>
extern "C" int nsmbu_r24_private_probe(void (*log)(const char*));
namespace {
void log_line(const char* s) { std::puts(s); }
}
int main() {
    dispatch::init();
    int rc = nsmbu_r24_private_probe(log_line);
    if (rc) std::fprintf(stderr, "R2.4 public dispatch wiring FAILED (%d)\n", rc);
    else std::puts("R2.4 PUBLIC SYNTHETIC DISPATCH WIRING PASS (not a game function)");
    return rc;
}
