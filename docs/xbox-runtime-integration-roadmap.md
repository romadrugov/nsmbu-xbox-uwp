# NSMBU Xbox Series S — runtime-first integration plan

## Scope and baseline (2026-11-09)

Goal: native Windows UWP x64 port of the existing NSMBU native
recompilation, **not** a Wii U emulator. Keep game RPX, generated code,
COFF objects and archives private. Never upload Nintendo-derived code
to this public repository, Actions artifacts, or public crash logs.

### What has actually been verified

* Physical Xbox Series S: v0.7.0 UWP smoke running; guest address
  reservation, a public synthetic recomp function, D3D12 checkerboard,
  gamepad A and the mapping of real USA_V0 .text/.rodata/.data/.bss
  with CRC32 and byte comparisons (4/4 sections).
* Mac M1, private workspace: exact-USA_V0 recompiler inventory
  (84,231 functions, 2,821 imports, 93 generated C chunks); all 93
  plus table.c and imports.c compiled to Windows GNU AMD64 COFF.
  The 95-object private archive is ~2.1 GB.
* No real Nintendo guest function has been linked into UWP or
  executed on Xbox. No game graphics, menus or audio run on Xbox.
* Stable Xbox rollback: xbox-uwp-stable-0.7.0.

### Important architecture finding

The main upstream runtime already exists in this fork. At the
audited source-tree heads, all of upstream's files under
runtime/src/core (18), runtime/src/hle (10), runtime/src/gx2 (12),
runtime/src/gfx/vulkan (52), runtime/src/audio (3), and
runtime/src/input (11) are byte-identical. We added separate Xbox
core and D3D12 probe files, but the UWP .vcxproj currently compiles
ONLY the smoke app, rpx/VM utilities, and the D3D12 probe.
It does not compile the upstream HLE, thread scheduler, GX2 core,
the normal renderer integration, game loader or full runtime.

Upstream's render::Backend in runtime/src/gfx/renderer.h is a useful
port boundary. Our gfx/d3d12/backend_probe.cpp only implements
synthetic clear/draw/swap; it explicitly rejects actual guest GX2
registers and shaders. Do not call it a production renderer.

## Workstream milestones, in order

| Gate | Deliverable | Acceptance evidence |
| --- | --- | --- |
| R0 — inventory / reproducibility | Audit component reuse, source files, dependencies, private-code boundary, rollback | This document; source-tree comparison; physical v0.7.0 log preserved locally |
| R1 — actual upstream code compiling for UWP | New separate static-library compile probe for upstream core/HLE modules | VS2022 UWP (not desktop) compile succeeds for selected real upstream sources; diagnostics enumerate blockers, without depending on game data |
| R2 — execution ABI/private link | Private MSVC/UWP-compatible game archive + generated table and imports integrated with the existing guest memory/runtime, no public game blobs | UWP x64 link, PE/import inspection, ABI + address dispatch checks, local hardware smoke with a controlled/known game function |
| R3 — game bootstrap and Cafe OS | Replace synthetic dispatch path with reusable upstream core, memory, threads, HLE, FS loader; write platform adapters for unsupported UWP calls | Real USA_V0 entry reaches traceable checkpoints and requests game resources; missing HLE calls counted by name, no crashes due to unknown imports |
| R4 — graphics adaptation | Wire actual GX2 to a complete backend using existing render::Backend and reused GX2 command/state/shader logic, retaining the D3D12 UWP host | At least one real GX2 frame rendered and presented (not synthetic checkerboard); texture/shader test cases; frame capture |
| R5 — input, audio, game loop | Xbox Gamepad → existing input map, HLE AX audio → XAudio2 or UWP-compatible output, save/load | Menu navigation, synchronized sound, save and reload on physical console |
| R6 — gameplay and quality | Profiling, correctness fixes, memory budget, packaging, regression smoke | First level playable, 60-fps target investigated/measured, no repeated critical crashes, private release validation |

Gate sequence is deliberately evidence-based. Do not equate
"95 compiled objects" or passing synthetic smoke tests with
percentage of a playable game. Some gates can overlap once their
interfaces are fixed.

## Engineering priorities

1. **Reuse upstream first.** Compile normal runtime core/HLE/GX2
   unchanged where possible; isolate platform-specific adapters.
   Only fork upstream logic when UWP APIs force it and document why.
2. **Use a separate UWP runtime build probe.** Avoid breaking the
   working smoke target until a new component compiles and tests.
   A source-only compile proves compatibility, NOT linking or game boot.
3. **UWP availability is a hard constraint.** Audit windows.h and
   desktop APIs in platform/host.h, core/core.cpp,
   core/threads.cpp, audio and SDL/Vulkan host paths. The retail PC
   build cannot simply be pasted into a Windows Store project.
4. **Handle compiler and ABI early.** Private 95-object archive came
   from Zig x86_64-windows-gnu and still has CRT, sanitizer and guest
   runtime imports. COFF is not proof of UWP/MSVC link compatibility.
   Prefer private clang-cl/MSVC ABI for the real Windows Store link.
5. **Renderer choice is not settled.** The upstream Vulkan renderer
   has deeply developed GX2-to-Vulkan implementation and the Metal
   backend exists, but Xbox UWP D3D12 has only a synthetic probe.
   Compare a real D3D12 backend against a separately verified Vulkan
   translation route; do not assume either is a drop-in port.
6. **Protect copyrighted input.** The physical Xbox reads the user's
   own private USA_V0 RPX from LocalState. No GitHub Actions run may
   access real game data. A private Windows/self-hosted environment
   may be required for private executable linking.

## Immediate next task (R1)

Build a separate UWP **static-library compile-only** probe for a
small set of real upstream sources (start with core/env.cpp, then
coreinit_mem.cpp). Use the same Windows Store SDK as our smoke app and
a public CI fixture; do not link in the game or modify the stable
AppX. The first goal is to discover precise compile incompatibilities
and measure what upstream code can be reused without patches.
Only after a green source compile should we increase the source set
towards core/core.cpp, threads.cpp and other HLE modules.

## R1 first executed probe — CI results

CI run: https://github.com/romadrugov/nsmbu-xbox-uwp/actions/runs/42747423853

* **PASS:** unchanged upstream \`runtime/src/core/env.cpp\` compiled by MSVC v143 under a Windows Store x64 static-library project. Only a project-local \`_CRT_SECURE_NO_WARNINGS\` flag was required (MSVC C4996). The original upstream file was NOT edited.
* **PASS:** unchanged upstream \`runtime/src/hle/coreinit_mem.cpp\` compiled to a Windows x64 COFF object with standalone \`clang-cl\` using the Windows APP API-family define, without generated Nintendo game files. This tests real Cafe OS heap/HLE code syntax and basic Windows-header portability.
* **Toolchain finding:** MSVC cannot parse several Clang attributes and builtins in \`ppc.h\`; MSBuild \`PlatformToolset=ClangCL\` is not installed on the default CI Windows Store image (MSB8020). Use standalone clang-cl for translated core/HLE sources, and MSVC v143 where WinRT project build tooling requires it.
* **NOT tested:** link/activation of this new HLE module inside the Xbox UWP executable, full PowerPC runtime memory safety, system-call correctness, Xbox hardware use, or executable game code. The current smoke AppX remains unchanged.

Next R1 increment: compile additional upstream HLE/FS/threads source modules under the same clang-cl path; investigate missing imports and UWP-forbidden desktop APIs before linking them into an isolated runtime library.

## Explicitly not claimed

* 10–15% or any other overall completion estimate is not measured.
* Native UWP gameplay is not achieved by archiving compiled COFF.
* Compiling a C++ source as a Windows Store library does not prove
  it can link to the real game or run on the physical Xbox.
