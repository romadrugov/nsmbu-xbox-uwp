#pragma once

#include <unknwn.h>

namespace nsmbur9 {
using Log = void (*)(const char*);
// Caller must be the owning UWP CoreWindow UI thread. The window pointer
// is an IUnknown* from CoreWindow^, held alive for the whole call.
// No RPX/game code is invoked by this independent graphics integration test.
bool RunDozenFrame(IUnknown* coreWindow, Log log) noexcept;
}
