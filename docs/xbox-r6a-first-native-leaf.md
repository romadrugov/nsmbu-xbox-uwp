# R6A — first real recompiled game function on Xbox (one vetted leaf)

## Current baseline

R5F was validated on the owner's Series S: the local 1280×720 PNG was decoded
and presented with D3D12 after SHA-verified USA_V0 RPX sections and R5B CPU
bootstrap were successfully staged. R5F does **not** execute any game function.
Rollback source is public `7ee6ad16fb1cc2509666ef774f32cae06e1c5d48`.

## R6A technical goal

Execute exactly **one** owner-derived native x64 function from the existing
84,231-entry recompilation, without ever entering the main program, loading
assets, faking Cafe OS calls, or attempting unbounded execution.

Game-derived code stays inside the owner's private CI and signed AppX.
No Nintendo game bytes or generated C files are added to this public repository.

The experiment runs only if **all** of these conditions hold:

1. The owner explicitly dispatches a private build with
   `enable_r6a_leaf=true`, which passes `-EnableR6Leaf` to the compiler.
2. RPX is recognized by the exact SHA256 as `USA_V0`, has the known entrypoint,
   and all six executable/data staging checks pass (including MEM2 zero-fill).
3. R5B CPU initialization succeeds and the R5D relocation metadata audit
   reports no malformed references, out-of-target entries or missing sections.
4. In the **verified decoded owner-local .text bytes**, the function table
   contains a registered actual native function whose PPC body starts with
   either `li r3, signed-immediate; blr` (preferred) or a single `blr` opcode.
   Both patterns return unconditionally without touching guest memory or HLE.
5. The runtime checks that the function-table address and pointer are identical
   to the native dispatcher before invoking the actual recompiled function.

It clones the prepared CPU, sets a sentinel in register r3 and performs
exactly **one** native call. The result is checked: r3 must be the immediate
or unchanged sentinel; r1, r2 and r13 must not change. A no-match outcome is
reported as **no execution**, not success. A runtime error or mismatch is not
treated as a successful execution.

This is NOT a game frame, menu, or game boot. It makes no claim that the
selected leaf is a core gameplay function. Real Cafe OS services, scheduler,
data-imports, graphics and actual startup execution remain separate projects.

## What public CI can prove

The branch's public `xbox-first-real-link-preflight.yml` compiles and links
the exact opt-in bridge using the author-written synthetic PPC fixture.
It never includes private game code and therefore cannot prove that the
owner's game has a matching leaf, or that such a leaf works on hardware.

## Private dispatch

The owner-private workflow `nsmbu-usa-v0-full-link.yml` will provide a
boolean `enable_r6a_leaf` input (default `false`), which requires
`build_r4_appx=true`. Build a **0.8.0.0** package only after public CI
passes and the private workflow pins an immutable R6A SHA.

Use the existing owner-controlled boot-first release and SHA256. The output
is still `nsmbu-r4-private-boot-preflight-appx` (historical artifact name).

## Physical-console success

After installing the opt-in build, read:
`LocalState/nsmbu_r6a_first_native_leaf.txt`.

A real R6A success line must have:

```
[rpx/R6A] REAL GAME LEAF EXECUTED: addr=........ pattern=2 r3=........ expected=........ stack-preserved=1
```

If no verified leaf exists, the correct outcome is:

```
[rpx/R6A] NO vetted leaf in USA_V0 table; game execution not attempted
```

Do not confuse R5F `R5F PRESENT PASS` with the new CPU execution result.
Both may appear in the same package, because the D3D12 preview is preserved.

## Limitations / risk

The instruction-pattern check substantially reduces risk, but cannot prove
host execution will never fault: this is experimental native code on Xbox.
Never enable this for unknown RPX builds or arbitrary addresses; do not
apply relocations globally or call the game entrypoint as an alternative.
If unsupported, revert to the previously working R5F signed package.
