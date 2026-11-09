# NSMBU Xbox Series S: UWP port experiments

Work only in the `xbox-uwp` branch for now. The upstream native NSMBU
recompilation is **not** currently running on Xbox.

## Milestone 0 — platform smoke test

`xbox/smoke/` is a completely separate C++/CX Windows 10 UWP app.
It creates a real D3D12 device, command queue, CoreWindow swapchain, RTV,
fence, and continuously presents alternating blue/teal frames.

It intentionally contains **no Nintendo data or game code**.
This lets us test the Xbox Developer Mode toolchain, AppX packaging,
launch, D3D12 support and diagnostic output without complicating results
with the Wii U recompilation runtime.

### Build and install

Push to `xbox-uwp` to run the **NSMBU Xbox - D3D12 Smoke** workflow.
On a successful run, download the `nsmbu-xbox-smoke-...` artifact
from the workflow's **Actions** page. It contains a signed `.appx`
and its public `.cer` certificate. Install using the Xbox Device Portal
in Developer Mode, with certificate if prompted.

Expected result: full-screen blue/teal changing every second.
A static splash or a crash is **not** a pass. This is not NSMBU gameplay.

A failed GitHub Actions build is also useful; inspect its first compiler,
linker, or AppX packaging error and fix the underlying dependency before
proceeding.

### Local (Windows / Visual Studio 2022)

Generate assets: `py -3 xbox/smoke/make_assets.py`.
Build `xbox/smoke/NSMBUXboxSmoke.vcxproj` as Release|x64 with
C++ UWP workload and Windows SDK 10.0.22621 (or override the installed SDK).

## Milestone 1 — port the runtime

Only after Milestone 0 works **on an actual Series S**:

1. Isolate the host interfaces in `runtime` (files, input, audio, windowing)
   so they can be implemented for UWP without importing Win32 desktop APIs.
2. Evaluate the viability of Mesa Dozen (Vulkan over D3D12). Measure actual
   Vulkan features used by NSMBU first; do not assume desktop Vulkan will work.
3. Keep the PowerPC → C recompilation independent of graphics, and validate
   Clang's `musttail` support with an Xbox x64/WindowsStore target.
4. Build and link guest stubs first; only then add locally generated game code.

### Safety and data boundaries

Never put Wii U dumps, extracted `code/content/meta`, keys, `.rpx`,
generated `code_*.c` or game resources in GitHub commits or CI artifacts.
The public workflow packages **only** this independent smoke test.
Keep your tested `USA_V0` files on your Mac.
