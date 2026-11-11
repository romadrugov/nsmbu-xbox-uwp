// Public ABI + Windows Store executable link check for REAL upstream env.cpp.
// Must resolve actual external functions and data from NSMBURuntimeCompat.lib,
// rather than succeeding with an unused / unreferenced archive.
#include "core/env.h"
#include <cstring>
#include <string>

// Public metadata-only WinRT marker: emitted to satisfy UWP build plumbing.
// This does not make the probe an activatable, deployable Xbox application.
namespace NSMBURuntimeLinkProbe {
public ref class MetadataMarker sealed {
public:
    MetadataMarker() {}
};
}

#ifdef NSMBU_PRIVATE_GAME_LINK
// Author-created bridge compiled by clang-cl. This checks a privately
// supplied recomp table but NEVER executes a Nintendo game function.
extern "C" int nsmbu_private_game_link_metadata();
#endif

int main() {
    const char* literal = env::expand_path("no-expansion-needed");
    if (!literal || std::strcmp(literal, "no-expansion-needed") != 0)
        return 1;
    if (!env::vulkan_cpu_paths[0].name())
        return 2;
    // env::unset is a real external function, but must not be called at
    // runtime in the UWP probe: MSVC must only resolve its link symbol.
    auto* volatile unlink_test = &env::unset;
    (void)unlink_test;
#ifdef NSMBU_PRIVATE_GAME_LINK
    if (nsmbu_private_game_link_metadata() != 0) return 30;
#endif
    return 0;
}
