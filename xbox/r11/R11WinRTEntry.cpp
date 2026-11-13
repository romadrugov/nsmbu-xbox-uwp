// R11: link the real full NSMBU Recomp game entrypoint, not a synthetic
// PPC dispatcher, SDL window, PNG, or no-op gameplay stub.
//
// The SDL3-uwp static library is built separately with MSVC /ZW and owns
// CoreApplication::Run / CoreWindow; we delegate via its real SDL_RunApp.
// This source is pure C++20 (clang-cl) and intentionally contains NO
// Nintendo-derived code. Game entry is from runtime/src/main.cpp.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SDL_MAIN_HANDLED 1
#include <windows.h>
#include <SDL3/SDL_main.h>
#include <atomic>
#include <cstdint>

#if !defined(NSMBU_XBOX_UWP)
#error "R11 entrypoint must not be linked into a desktop application"
#endif

// The game is compiled with its original main(int,char**) by clang-cl.
// Binding this symbol forces the linker to extract the real main.cpp
// object and its transitive Cafe OS, GX2 and Vulkan runtime dependencies.
extern "C" int main(int argc, char** argv);

// Smash4's patched Mesa Dozen UWP WSI dynamically locates these optional
// exports on the executable. R9 proved this mechanism on Series S.
// This initial fallback is 1280x720; actual dimensions should come from
// the SDL3 WinRT window when the full runtime starts creating surfaces.
namespace {
std::atomic<unsigned> g_width{1280};
std::atomic<unsigned> g_height{720};
}
extern "C" __declspec(dllexport) unsigned uwp_get_width() {
    return g_width.load(std::memory_order_relaxed);
}
extern "C" __declspec(dllexport) unsigned uwp_get_height() {
    return g_height.load(std::memory_order_relaxed);
}

// WindowsStore GUI/CRT invokes WinMain. The pinned SDL3-uwp fork exposes
// the *four-argument* SDL_RunApp(argc,argv,mainFunction,xamlBackgroundPanel).
// It creates the non-XAML WinRT app and invokes the actual game main().
// No native game data may be passed into the publicly compiled wrapper.
extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    OutputDebugStringA("[R11] SDL3 WinRT bootstrap -> REAL NSMBU main()\r\n");
    return SDL_RunApp(0, nullptr, main, nullptr);
}
