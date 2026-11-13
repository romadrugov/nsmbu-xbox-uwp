# R7C — clear observed coreinit.OSIsDebuggerInitialized blocker

## Evidence from actual Series S R7B run

Owner log `nsmbu_r7b_real_entrypoint.txt`:

```
[rpx/R7B] MAIN OSThread READY: addr=60100000 bytes=1696 ...
[rpx/R7] STOP: setup=0 status=2 entry=02AA2E50 last-pc=02AA2E50 ... coreinit-handled=1
[rpx/R7] FIRST BLOCKER: ... missing HLE coreinit.OSIsDebuggerInitialized
```

This means the real entry was *attempted* and the real guest-backed
`OSGetCurrentThread` was handled; it does **not** mean menu, level, or game frames
have run.

## Scope

The exact upstream implementations in `runtime/src/hle/coreinit_misc.cpp`
return `0` for `OSIsDebuggerInitialized` and `OSIsDebuggerPresent`,
`1` for `OSGetMainCoreId`, and use a full sequential-consistency
fence for `OSEnforceInorderIO`. R7C mirrors those **four simple,
deterministic** services inside the already owner-opted-in
`hle_unimplemented` interception layer in
`xbox/runtime-compat/bootfirst_runtime.c`.

The existing three R7B functions remain:
`OSGetCurrentThread` (guest-backed thread), `OSGetCoreId`
(`Cpu.core`), and `OSMemoryBarrier` (real fence).

- No blanket success for unknown HLEs.
- No invented scheduler/heap/GX2 or actual graphics frames.
- Existing strict USA_V0 RPX SHA verification, R7 opt-in, bounded
  execution and SEH stop remain unchanged.
- Nintendo data and owner-local game assets remain outside public GitHub
  and public fixture CI.

## Build and test

1. Run public Xbox first-link/preflight GitHub Actions on this PR.
   Passing CI proves public *synthetic* code compiles, links, and packages
   through the same UWP shell; it does not prove commercial game behavior.
2. Merge after CI passes; pin the **merged immutable public commit SHA**
   in the owner-private `mkwii-xbox-build` workflow.
3. Retain private opt-in `enable_r7_entry_probe=true`, the owner-provided
   USA_V0 input, and signed AppX build.
4. Install AppX **0.9.2.0** on Xbox Series S, retaining local
   `red-pro2.rpx` and `nsmbu_preview.png` in LocalState.
5. Read **`LocalState/nsmbu_r7c_real_entrypoint.txt`**.

Expected next diagnostic if debugger query is reached:

```
[rpx/R7] STOP: ... coreinit-handled=2 ...     # or greater
[rpx/R7] FIRST BLOCKER: ...                    # different from OSIsDebuggerInitialized
```

If the new log still stops on that exact debugger query, check that the
owner-private archive and AppX actually use the new merged public SHA
with `NSMBU_R7_ENTRY_PROBE` defined. If a different missing HLE or
guest memory fault appears, fix that *actual next blocker* in a later stage.
If startup crashes, hangs, or package install fails, keep the log and roll
back to the owner-validated R7B AppX 0.9.1.0.

A picture on screen is still the R5F locally supplied PNG, **not** a game frame.
