# Xbox headless runtime bootstrap — v0.5.0

This milestone validates the next CPU/runtime layer, **not a playable NSMBU build**.

## Architecture

- `recomp/android_fixture.py` generates a public synthetic PowerPC program (return 42).
- The real recompiler generates `code_NNN.c`, `table.c` and `funcs.h`.
- `build-ppc-fixture.ps1` compiles both translated functions and the generated
  `table.c` with clang-cl, together with
  `runtime/src/core/headless_dispatch.c`. The resulting static library
  links into the existing UWP/C++/CX launcher.
- `nsmbu_headless_public_boot` reads `g_recomp_entry_point`, initializes a
  table by **guest addresses** from `g_recomp_funcs` and
  `g_recomp_imports`, then dispatches through `ppc_dispatch`.
- A *synthetic* host callback is registered at the HLE region
  `0xC2000000`. It adds 8 to the result of the generated function,
  demonstrating the dispatch path from 42 to 50. It is **not** an
  actual Wii U OS import.
- `GuestVm` in `runtime/src/core/xbox_guest_vm.*` keeps a fixed 4 GiB
  virtual guest reservation at the expected PPC memory base for the
  lifetime of the test. Only explicitly requested 64-KiB ranges are
  committed to physical memory, with a no-access null guard. It tests
  two distinct addresses (MEM2 and foreground region). This is
  **explicit on-demand commit**, *not* page-fault-driven lazy commit.

## What a successful Xbox log should contain

```text
NSMBU Xbox Headless Runtime Bootstrap v0.5.0 started
[boot/0.5] guest VM FIXED 4GiB reserve PASSED
[boot/0.5] guest VM two sparse 64KiB commits PASSED (MEM2/foreground)
[boot/0.5] table: entry=02000000 funcs=1 imports=0; native guest r3=42; host slot=C2000000 r3=50; unknown rejected=1
[boot/0.5] guest-address dispatch + synthetic HLE callback PASSED (42 -> 50)
[boot/0.5] Phase 3: preserve verified D3D12 graphics path
```

As in v0.4.0, the checkerboard should render and Xbox A should change the background.

## Not yet implemented

Loading the user's actual `red-pro2.rpx`, commercial assets, Wii U
imports/HLE services, guest thread scheduler, actual game table generated
from the user's own dump, automatic memory fault handling, and gameplay
rendering. No copyrighted game files are uploaded to the repository or CI.

The last user-tested stable baseline is
`xbox-uwp-stable-0.4.0`.
