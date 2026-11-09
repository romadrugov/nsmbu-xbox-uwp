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

### Diagnosing Xbox startup exits (diagnostic build 0.1.1)

The first smoke app exited on the console immediately after its splash screen.
The diagnostic build logs every D3D12 initialization HRESULT and first presented
frame to `LocalState/nsmbu_xbox_diag.txt`.

1. Deploy the **0.1.1** AppX from the latest successful `NSMBU Xbox - D3D12 Smoke` run.
2. Launch on Series S. If rendering fails, it should now remain open with a
   blank view rather than exit automatically; this is intentional.
3. On the Mac, open `https://<console-ip>:11443` → **File Explorer** →
   **LocalAppData** → **Romadrugov.NSMBUXboxSmoke** (or the package's full identity)
   → **LocalState**. Download `nsmbu_xbox_diag.txt`.
4. Share the contents, especially the last successful API and any `0x...` HRESULT.

If the file is **missing**, the app likely fails before `Run()` can initialize
LocalState, so check the Device Portal's crash/error diagnostics instead.
Don't confuse the splash screen with a successfully presented D3D12 frame.


## Milestone 0.5 — D3D12 GPU probe v0.1.5

v0.1.4 is **verified on Xbox Series S**: it stays open on a blank black screen,
confirming our AppX, WinRT activation, `resources.pri`, `.winmd`, GUI subsystem,
and the `Microsoft.VCLibs.140.00` framework dependency are correct.

The v0.1.5 probe switches to `MainGpu.cpp` while retaining the working
`OnActivated`/`IFrameworkView` lifecycle. It creates a D3D12 device,
CoreWindow swap chain, RTV heap and GPU fence, then alternates teal
clear colors at 60-frame intervals. It is a platform test, **not NSMBU**.

Deploy `NSMBUXboxSmoke_0.1.5.0_x64.appx` (and the VCLibs dependency if it is
not already installed) from the Xbox D3D12 GPU Probe workflow. Certificate
changes each CI run: uninstall the prior smoke package if an upgrade fails.

- **Success:** teal/blue background stays visible and changes tone.
- **Failure, app remains black:** check Xbox Device Portal → File Explorer →
  LocalAppData → `Romadrugov.NSMBUXboxSmoke` → LocalState →
  `nsmbu_xbox_diag.txt`. Look for the last `GPU stage` or HRESULT.
- **Immediate exit:** launch or dependency regression; compare with v0.1.4.

This retains the known-good lifecycle-only source in `MainLifecycle.cpp`,
so a subsequent experiment can switch back without losing the baseline.

## Milestone 0.6 — Shader + Gamepad Probe

Tests the full native D3D12 graphics pipeline by drawing an original, multicolored
triangle with compiled HLSL Shader Model 5 vertex/pixel shaders. Uses the known
working v0.1.5 swapchain. The Xbox controller's A button is sampled through
`Windows.Gaming.Input.Gamepad`. Hold **A** to change the background to green;
release to return to pulsing blue. If Gamepad fails, the triangle should still
render and an HRESULT is recorded once. `MainLifecycle.cpp` is retained as a
known-good fallback, and the app still contains no Nintendo game data.

## Milestone 1.0 — first shared NSMBU graphics code on Xbox (0.1.7)

`runtime/src/gfx/d3d12/format_map.{h,cpp}` is the first reusable part of
the future renderer backend. It converts a **deliberately limited**, verified
subset of Wii U GX2 numeric pixel formats into native DXGI format enums:
RGBA8, R8, RG8, R16, 16/32-bit float formats and BC1–BC5 blocks.
All unimplemented / conversion-requiring formats explicitly return
`DXGI_FORMAT_UNKNOWN` rather than silently producing incorrect colors.

The 0.1.7 probe compiles the actual source file into the existing UWP
app and runs a runtime self-test for RGBA8, sRGB, BC1, BC5 SNORM, D16
and unsupported RGB565, logging the result in `LocalState`.
The previous triangle and A-button test remain intact.

**Not implemented yet:** Wii U texture tiling/swizzling, endian fixes,
D24S8 conversions, GX2 shader decompilation, descriptor bindings,
the full `render::Backend` dispatch, or loading commercial game assets.
This is the start of a real game-facing D3D12 layer, not the finished port.
