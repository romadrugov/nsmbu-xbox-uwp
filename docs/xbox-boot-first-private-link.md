# Xbox NSMBU boot-first — frozen checkpoint and real-game link path

## Recovery checkpoint (2026-11-11)

* Permanent commit: \`c5f18ac960ab6cbdebcd453306e249e248edff45\`
* Branch: [xbox-uwp-checkpoint-r2-2026-11-11](https://github.com/romadrugov/nsmbu-xbox-uwp/tree/xbox-uwp-checkpoint-r2-2026-11-11)
* Physical Xbox rollback: \`xbox-uwp-stable-0.7.0\`.
* Public synthetic PowerPC -> real upstream Cafe OS HLE desktop test:
  GitHub Actions run 43054420514 PASS. No Nintendo game code executed on Xbox.

Use the permanent SHA if the branch name is ever changed. Do not
destructively reset a checkout containing uncommitted work.

## Why a complete real-game AppX cannot yet be delivered from PUBLIC CI

The user's verified USA_V0 game recompilation is in their local Mac
workspace, not in GitHub: 84,231 functions, 2,821 imports, 93 generated
code chunks and table.c/imports.c (95-object Windows-GNU archive).
The public CI may use ONLY the three-instruction authored PPC fixture.

Our Xbox UWP shell, guest VM and real Cafe OS heap ABI exist. But
gamecode is not yet privately linked against all production runtime
dependencies, GX2 graphics is not implemented in a real D3D12
renderer, and no Xbox test has called actual game code.

**Boot-first** means immediately attempting the private native LINK,
then iterating on real link/runtime errors. A successful link check
alone is NOT a playable game.

## A. Fastest link attempt using EXISTING archive

On a private Windows x64 machine/VM with VS2022 C++ UWP build tools,
Windows SDK, Python and LLVM clang-cl, transfer only the user's
private archive from Mac:

    ~/NSMBU-Xbox-private/USA_V0/mac-probe/nsmbu_usa_v0_win.lib

to a private Windows folder outside the public checkout,
then run from an x64 VS Developer PowerShell:

~~~powershell
.\tools\xbox\try_private_game_link.ps1 -PrivateArchive 'D:\NSMBU-private\nsmbu_usa_v0_win.lib' -OutputDir 'D:\NSMBU-private\first-link-gnu'
~~~

It links the ACTUAL supplied archive into an AppContainer x64
**diagnostic link probe**, with no game-code execution. Private
binary and full linker logs stay local to OutputDir. The script
prints the first few unresolved symbols and writes a safe
\`link-summary.json\` with booleans only. Never post full
\`private-link.log\`, private .lib/.obj, or executable.

## B. If Windows-GNU archive is incompatible with Windows Store CRT

The existing Zig-built COFF archive uses GNU Windows target. If
required, rebuild all 95 modules with clang-cl/MSVC ABI, privately:

~~~powershell
py -3 tools/xbox/private_compile_all.py --generated 'D:\NSMBU-private\USA_V0\generated' --summary 'D:\NSMBU-private\USA_V0\summary.json' --out 'D:\NSMBU-private\USA_V0\msvc-archive'
.\tools\xbox\try_private_game_link.ps1 -PrivateArchive 'D:\NSMBU-private\USA_V0\msvc-archive\nsmbu_game_msvc.lib' -OutputDir 'D:\NSMBU-private\first-link-msvc'
~~~

Sources and logs must remain outside checkout. No upload to public
GitHub Actions, Issues or chat attachments. The compiler checks
verified USA_V0 summary and produces AMD64 COFF + an MSVC .lib.

The public workflow \`.github/workflows/xbox-first-real-link-preflight.yml\`
tests the same linker/build mechanics using a PUBLIC three-instruction
PPC fixture only. It is NOT a real Mario game build.

## What we do after link diagnostics

1. Integrate genuine production dispatch, Cafe OS runtime and
   guest memory initialization rather than synthetic shims.
2. Verify a bounded *real* game function on a physical Xbox with
   crash-safe logging and private game files. Do not run the entry
   point without runtime prerequisites.
3. Adapt existing GX2 logic to a real UWP-compatible D3D12 renderer;
   then work toward menus, input, audio and a playable level.

No date or completion percentage is implied by a successful archive
link. A 2.1 GiB static library is not a ready-to-install AppX.
