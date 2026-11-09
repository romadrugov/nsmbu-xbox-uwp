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


## Milestone 1.1 — GPU texture upload and sampling (0.1.8)

Added reusable linear RGBA8 upload code in `runtime/src/gfx/d3d12/texture_upload.h` and `.cpp`.
It creates a DEFAULT-heap GPU texture, computes padded row strides via `GetCopyableFootprints`,
copies linear CPU pixels into an UPLOAD buffer, executes `CopyTextureRegion` and a transition
to `PIXEL_SHADER_RESOURCE`, retaining the staging resource until the upload fence completes.
The self-generated 48x48 checkerboard has 192 bytes per original row and 256 bytes per GPU row,
so this probes the D3D12 alignment rule, not just texture creation.
A shader-visible SRV descriptor at t0 and a point static sampler at s0 provide textured drawing;
the Xbox controller A button continues to change the background color.

**Expected Xbox result:** a large square cyan/cream checkerboard over the pulsing teal background.
The log should include `RGBA8 checker texture upload COMPLETE: 48x48, source row=192, GPU row=256`.
This is not GX2 untile/swizzle support, mipmapping, resource caching or full shader translation yet.

## Milestone 1.2 — CPU-side GX2 1D thin tiled decoding (v0.1.9)

`runtime/src/gfx/d3d12/gx2_detile.{h,cpp}` adds a bounded, pure-C++
single-mip RGBA8 surface decoder for exactly two GX2 tile modes:

- `GX2_TILE_MODE_LINEAR_ALIGNED` = 1: uses explicit surface pitch
- `GX2_TILE_MODE_TILED_1D_THIN1` = 2: 8x8 microtiles and the 32-bit
  color microtile bit sequence documented in vendored Cemu
  `LatteAddrLib_Coord.cpp`.

This does **not** decode mode 0 (DEFAULT), macro-tiled modes 4–15,
compressed formats, cube/3D textures, multisample surfaces, or mips.
Unsupported modes are rejected, without attempting to guess.

`runtime/test/gx2_detile_test.cpp` runs in independent Ubuntu CI,
with golden 8x8 microtile indices, irregular 13x9 texture dimensions,
padded pitch, and invalid/truncated input checks. On Xbox v0.1.9
reconstructs the 48x48 checker through synthetic GX2 1D tiled data,
compares it byte-for-byte against the original, then uploads the decoded
texture to D3D12. This preserves the previously verified SRV and GPU
rendering pipeline.

Expected `LocalState/nsmbu_xbox_diag.txt` log:
`GX2 detile CPU self-test PASSED (linear aligned, 1D thin1, 13x9, bounds)`
and
`GX2 detile 1D thin1 48x48 RGBA8 roundtrip PASSED`.
The visible checkerboard should remain identical to v0.1.8.

## Milestone 1.3 — GX2 2D macro-tiled texture decode (v0.2.0)

`runtime/src/gfx/d3d12/gx2_macro_detile.{h,cpp}` decodes GX2 tile mode
4 (`2D_TILED_THIN1`) for single-slice, uncompressed RGBA8 images.
It implements Cemu's coordinate-address mapping for the Wii U's 2 GPU
pipes, 4 memory banks, 256-byte pipe interleave, GX2 swizzle bits 8–10,
and 32x16-pixel macro tiles.

An explicit `gx2_swizzle` parameter is mandatory (there is no guessed
default). Pitch must be a multiple of 32 texels and the source buffer
must contain full padded 16-row macro-tile groups. All unsupported
modes, missing surface padding, mipchains, non-RGBA8 formats, slices
and AA samples remain out of scope.

`runtime/test/gx2_macro_detile_test.cpp` tests independently fixed
Cemu-derived byte-address vectors for swizzles `0`, `0x500`,
and `0xD0500`, dimensions 53x37 with 64-texel pitch, plus truncated
and invalid inputs. This is compiled with `-Wall -Wextra -Werror`
on Ubuntu CI alongside the old 1D tests.

Xbox 0.2.0 builds an independently encoded synthetic 48x48 RGBA8
checkerboard (64-texel pitch, swizzle `0x500`), validates the decoded
pixels byte-for-byte, and passes the result into the existing GPU
upload/SRV/pixel-shader pipeline. The visible checkerboard and A-button
background color change should stay the same as 0.1.9.

Expected log:
`GX2 macro 2D thin1 48x48 RGBA8 swizzle 0x500 roundtrip PASSED`.
No Nintendo assets, keys, or extracted game files are committed.
This does **not** yet integrate the renderer with `render::Backend`.


## Milestone 2.0 — game-facing render::Backend dispatch probe (v0.3.0)

The Xbox UWP test now links the actual runtime/src/gfx/renderer.h
render::Backend ABI and a partial adapter in runtime/src/gfx/d3d12/backend_probe.{h,cpp}.
Each frame it dispatches render::clear_color, render::draw, and render::swap.
The adapter drives the existing Direct3D 12 texture renderer.
Api::D3D12 is a distinct enum value (not disguised as Vulkan).
A Linux host unit test validates dispatch, counters, and strict rejection.

IMPORTANT: This is NOT yet a playable GX2 backend. It accepts only the
synthetic sentinel values kProbeColorTarget and kProbePrimitive.
Normal guest draw calls, registers, index buffers, color/depth targets,
shader bytecode, surface copies, and other Backend operations remain
unsupported. Most other Backend function pointers are deliberately null.
The full renderer.cpp and GX2 decompiler are not linked into the standalone
Xbox smoke test; it supplies a test-only render::g_backend symbol.
Production renderer selection intentionally does NOT use this partial table.

Expected on Xbox: identical checkerboard and A-button response to v0.2.0.
Log checkpoints: 'render::Backend D3D12 probe attached' and
'render::Backend dispatch first clear/draw/swap PASSED', plus increasing
'Presented frame' counters. Frozen rollback: xbox-uwp-stable-0.2.0.
Next: real GX2 color buffers, draw-state decoding and shader translation.
