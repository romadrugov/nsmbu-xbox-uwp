# R8 — full NSMBU Recomp runtime on Xbox UWP, via Vulkan/Dozen/D3D12

**Branch:** `feat/r8-full-runtime-dozen` starts from validated Xbox R7D
`62fd5ed993611dfdf20e0591381240f4072b821a`. Keep the signed R7D
0.9.3.0 package and its `LocalState` as rollback, and never merge experimental
R8 directly to `xbox-uwp` before a real console check.

Owner hardware R7D evidence:
```
entry=02AA2E50; entered=8413; loops=1338; coreinit-handled=4
MEMORY HLE: completed=2
first blocker: missing HLE coreinit.OSInitMutex at 029BA474
R5F PRESENT PASS: local PNG only, NOT gameplay
```

## Source inspection (pinned public references)

- [NSMBU Recomp](https://github.com/nsmbu-recomp/nsmbu-recomp),
  inspected `main@f99ee839b2a2a2f83b81fa27227b312fc4718156`.
- [Smash4 Xbox](https://github.com/jajdp/smash4-xbox),
  inspected `main@50ed2e91d9ae0aadfac5cf48777c8a825c5231e9`.
- Existing fork `runtime/src/core/core.cpp`, `runtime/src/hle/coreinit_mem.cpp`,
  `runtime/src/hle/fs.cpp`, `runtime/src/hle/ax.cpp`,
  `runtime/src/gfx/vulkan/backend.cpp`, `loader.cpp`, `present.cpp`,
  `surfaces.cpp` are identical Git blobs to upstream. Do not rewrite them.
- Upstream full runtime `CMakeLists.txt` builds owner private `code_*.c` +
  `table.c`/`imports.c`, `threads.cpp`, `coreinit*`, `fs.cpp`, GX2,
  vendor Cemu Latte shaders, Vulkan and SDL3. Our current UWP preflight links
  *none* of the complete runtime components; it links `bootfirst_runtime.c`
  (headless, missing scheduler) and a PNG preview instead.
- Smash4 is *different game/runtime*. Reuse its Mesa UWP toolchain/patches
  and AppContainer design, NOT its game-specific shader ABI or guest-memory
  renderer patches.
- Smash4 author's report: Series X Developer Mode, Vulkan 1.2.354 via
  Mesa Dozen 26.2.x => D3D12, real menu/demo at 23-32 fps; **Series S not
  tested**. This is strong precedent but not proof of Series S NSMBU support.
- Smash4 Xbox reports no buffer-device-address, external-memory-host or
  descriptor-indexing support on that console; a 128 MiB storage-buffer
  range cap; VK_KHR_swapchain/dynamic_rendering available. NSMBU renderer's
  current source does not request those first three extensions. It supports
  render-pass fallback for Vulkan 1.1/1.2 without dynamic rendering.
  That improves feasibility but DOES NOT prove compatibility.
- NSMBU `backend.cpp` still asks Vulkan instance 1.3, SDL3 Vulkan surface
  and a desktop loader. Xbox may require adjusting instance-version selection,
  loading `vulkan_dzn.dll` via UWP-compatible shim and WSI on CoreWindow
  thread. This must be tested on a physical Series S.
- The desktop `core.cpp::mem::init` commits a full 4 GiB on Windows.
  R7D uses a working 4 GiB reserved sparse guest VM. Adapt full runtime
  to reserve/commit safely, rather than combining both independent VM
  implementations or committing 4 GiB without measuring console limits.

## Integration, by *major subsystem*, not by one Cafe OS function

### 1. Full upstream runtime compilation with USA_V0 private game code

**Already added** an independent, manually dispatched private workflow:
`romadrugov/mkwii-xbox-build/.github/workflows/nsmbu-r8-full-runtime.yml`.
It verifies the owner archive SHA and compiles the full native Windows
NSMBU target using its 93 real generated code chunks. It does not
upload the private executable or change R7D and cannot produce Xbox
graphics. Fix full-engine compiler/link ABI issues in bulk here.

### 2. Port complete host to an ALTERNATE UWP target

Create a distinct `NSMBUFullRuntime` target; retain R7D
`NSMBUBootPreflight` intact. Compile/link real `core.cpp`,
`threads.cpp`, `hle/*.cpp`, `gx2/*.cpp`,
`gfx/vulkan/*.cpp`, `main.cpp`, owner `gamecode` and their
dependencies. Replace the current headless dispatcher completely:
the real runtime has symbols `ppc_dispatch`, `hle_unimplemented` and
host globals which would clash or have different semantics otherwise.

UWP host supplies `CoreApplication` activation, LocalState logging,
safe game `code/content/meta` paths, real guest memory pages, and the
actual `run_main` scheduling path. Use owner-provided extracted game
resources; `red-pro2.rpx` alone cannot render real NSMBU scenes.

### 3. Use Mesa Dozen to preserve the *existing* Vulkan GX2 renderer

Do not write a new GX2 -> D3D12 renderer from scratch yet. Reference
Smash4 Xbox:
`tools/compilar-dozen-uwp.ps1`,
`patches/mesa-dozen-xbox.patch`,
`src/mesa_sysstubs.c`,
`src/mesa_uwp_compat.h`,
`tools/compilar-uwp.ps1` and `docs/HOW-IT-WORKS.md`.

Prepare Dozen built in VS `x64 store` / AppContainer with the correct
DXIL DLL, vulkan shim or in-process loader, UWP-built SDL3, glslang,
Vulkan headers and shader SPIR-V stack. Pin a compatible Mesa commit
before applying its patch; do not blindly apply Smash4PC's game patch.

*Hard hardware gate on the user's Series S*: instantiate Dozen,
enumerate Vulkan capabilities and device/instance extensions, create
a D3D12-backed surface/swapchain on the UWP window thread, successfully
compile a shader, draw and present frames. Check BC/depth formats,
render-pass fallback, memory budget and 32-bit guest-address access.
If this fails, capture actual error; do not claim Dozen works on Series S.

### 4. The first true NSMBU frame (acceptance)

Only a frame emitted from NSMBU's own GX2 command path, translated by
its Vulkan renderer and shown through Dozen/D3D12, counts. A PNG
preview, synthetic Vulkan triangle, compositor clear, or a successful
`vkCreateDevice` does *not* count.

After the first game-created frame: wire Xbox controller to upstream
SDL3/VPAD mapping, then audio (AX/SDL3), then save/load and stability.

## Decision

Prioritize real upstream full runtime + Vulkan GX2 renderer; do not
extend `bootfirst_runtime.c` one HLE at a time. Preserve R7D.
Use Cemu-UWP as external inspiration for Windows AppContainer/D3D12,
not a replacement for native recompilation.
