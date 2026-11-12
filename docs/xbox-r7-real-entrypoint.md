# R7 — actual USA_V0 game entrypoint attempt on Xbox (failure-first)

**Do not call this a playable port or full Cafe OS implementation.**

## Motivation / baseline
The owner's Xbox Series S confirmed R6A:
\`REAL GAME LEAF EXECUTED: addr=02002AA0 pattern=2 r3=00000000 expected=00000000 stack-preserved=1\`.
R5F PNG preview and R5E RPX staging passed on hardware. We want a single
larger experiment that attempts the real game entrypoint and identifies the
**first concrete runtime dependency** instead of another metadata-only audit.

## What this revision actually does

The existing private UWP AppX is extended behind a **separate explicit**
\`enable_r7_entry_probe=true\` opt-in (default false):

1. R4/R5E checks and local RPX SHA match USA_V0.
2. Stage the six boot sections and stack, initialize CPU registers, verify
   R5D RELA metadata. Do **not** blindly apply native PPC ELF relocations.
3. Use the existing actual native recompilation dispatch table and invoke
   the **real entrypoint 0x02AA2E50** with a zeroed argc/argv and an R5 CPU
   snapshot. This is different from R6A's isolated 2-opcode pure leaf.
4. Instrument translated loops at compile time with
   \`NSMBU_R7_GUEST_BUDGET\`. Instrumented function entry/loop counters,
   2.5 second cooperative time budget, and SEH guard stop execution at
   the first unsupported Cafe OS HLE, unknown dispatch, trap, budget, or
   Windows access violation. The exception is logged instead of
   pretending that the function returned successfully.
5. Preserve the working R5F local PNG preview after the attempt stops.

Important: the budget is **cooperative** at instrumented function/loop
points, not a preemptive guarantee against all possible native hangs. A
native fault beyond the protected guest call or a tight loop with no
instrumentation can still terminate or freeze the process. This is an
experimental AppX, not a stable game.

## Intentional limitations

- The existing lightweight Xbox headless dispatcher and minimal runtime are
  not a full UWP port of desktop \`core.cpp\`, \`threads.cpp\`, GX2, audio,
  filesystem, input or heap subsystem. Weak Cafe OS imports still stop.
- No assets or Nintendo-derived code enter the public repository or public CI.
- A successful public synthetic workflow only establishes that the R7
  control path compiles/links with the three-instruction *author-created*
  fixture, **not** that commercial game initialization will succeed.
- With missing Cafe OS, real menu and engine-generated frames remain
  unverified. The first observed blocker guides the next substantive fix.

## Expected owner-only log

\`LocalState/nsmbu_r7_real_entrypoint.txt\`

Look for:
\`\`\`
[rpx/R7] BEGIN owner-private real entrypoint attempt (no Cafe OS yet)
[rpx/R7] STOP: setup=0 status=2/3 entry=02AA2E50 last-pc=... entered=... loops=...
[rpx/R7] FIRST BLOCKER: exception=... host-pc=... reason=...
\`\`\`

\`status=1\` would mean native entrypoint returned, **NOT** gameplay.
\`status=2\` means missing HLE/trap/budget; \`status=3\` means Windows SEH
exception; a negative setup result means actual entry was *not* called.
Keep the log private if it contains sensitive game-derived names.

## Build / rollback

After PUBLIC CI success, merge this PR, pin the immutable source commit in
the *private* \`mkwii-xbox-build\` workflow, and enable the R7 workflow switch.
The 0.9.0.0 AppX is only built by private owner-controlled CI.
No private source, RPX, compiled objects or original game assets are checked
into this public repository.

Rollback: uninstall this experimental package if it crashes/hangs and
reinstall previously verified R6A (public source 1f65861e1f461283c91492115579ac0937675612).
Back up \`LocalState\` before replacing the AppX.

## Next iteration

Interpret the *first* real failure (HLE/coreinit, data import, uncommitted
memory, graphic calls, etc.) and integrate the relevant real desktop runtime
service into UWP, fixing actual dependencies rather than adding generic
preflight reports.
