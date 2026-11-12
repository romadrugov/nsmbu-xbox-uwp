#pragma once
#include "core/xbox_guest_vm.h"

// Reads ONLY the app's LocalState/red-pro2.rpx, if user placed it there.
// Public synthetic RPX is embedded by CI from recomp/android_fixture.py.
// No commercial code is executed or packaged.
namespace xboxrpxprobe {
// requiredBuild is enforced before any LOCAL guest memory staging; the older
// smoke test keeps its original unfiltered known-build behavior by default.
void run(xboxguest::GuestVm& guest, void (*log)(const char*),
         const char* requiredBuild = nullptr);
}
