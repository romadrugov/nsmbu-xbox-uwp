# R2.5 — first branch-bearing gameplay function (scouting only)

Confirmed prerequisites (2026-11-10):
- Xbox Series S R2.3: public PPC + real HLE/guest heap + write-watch + D3D12 PASS.
- Xbox Series S R2.4: genuine private USA_V0 function at 0x02002AB8 returned
  R3=00000000, R4=2468ACE0, LR=00000000, matching the Mac reference.
  Device log reported [r24] FINAL PASS, green frame presented.

## Goal

Advance beyond the R2.4 two-instruction pure register leaf. Discover a
more substantive PowerPC routine containing a *conditional branch*, but
do not prematurely execute a potentially unsafe dynamic control-flow
path or attempt to boot the entire game.

## Step 1 — metadata-only scan of private generated game code on Mac

From your R2.4 tools worktree, update to this isolated experimental branch:

    cd "$HOME/NSMBU-Xbox-r24-tools"
    git fetch origin xbox-r25-controlflow-scout
    git switch --detach origin/xbox-r25-controlflow-scout

Run a static, read-only scan of your same 93 previously generated chunks:

    python3 tools/xbox/r25_branch_scout.py \
      --generated "$HOME/NSMBU-Xbox-private/USA_V0/generated" \
      --out "$HOME/NSMBU-Xbox-private/USA_V0/r25" \
      --limit 15

The CLI prints the total number of functions examined, reasons for
rejection, and the top 15 metadata-only candidates (address, source chunk,
instruction count, conditional branch count, local goto count).
It saves a private report:
    ~/NSMBU-Xbox-private/USA_V0/r25/r25_branch_scout.json

This **does not copy any Nintendo-derived C**, extract source code,
compile, execute, run the game, or build an Xbox package. The report
contains only aggregate counts and candidate metadata. You may share
this textual scan output for analysis; not the generated C files.

## What is filtered

- Size between 8 and 96 PPC instructions by default (tunable, 4–256).
- At least one actual PowerPC conditional branch opcode, and a BLR.
- Goto targets must have labels inside the same function; every target
  must be strictly after the corresponding annotated branch source.
- No observed guest memory operations, external imports, other function
  calls, loops, unimplemented opcodes, or unknown C helpers.
- Result writes r3 or r4, not just a function with no observable register
  output.

IMPORTANT: these are heuristic static checks, not a formal proof of
reachability or correctness. Generated fallthrough/tail calls can be
reachable and metadata-only candidates are *REVIEW ONLY* even when
they pass the filter. Do not feed any of these into R2.4's 'extract'
or COFF compiler automatically.

## Next gates after candidate discovery

1. Manually audit one proposed function's C control-flow and generated
   PPC opcodes **locally**; reject uninitialized state, reachable
   fallthroughs, branch-to-import, stack/guest memory assumptions and
   unresolved targets. Do not send extracted C to public GitHub.
2. Build a bounded Mac reference test with multiple input vectors that
   exercise **both branches**, not just a single R3 value; compare CPU
   registers and condition flags with known results.
3. Compile the reviewed branch function to Windows x64 COFF while
   preserving the ABI; prefer appropriate Windows SDK support for
   anything beyond the narrow freestanding register-only helper.
4. Build a private Xbox R2.5 AppX with separate package identity, keep
   R2.3 and R2.4 physically verified baselines installed.
5. The device test succeeds only when real hardware logs match **every**
   reference vector. Red/green alone is insufficient.

This branch is tooling-only. No physical R2.5 Xbox test has occurred.

CI (public generated fixtures, no proprietary game code):
https://github.com/romadrugov/nsmbu-xbox-uwp/actions/runs/42860806845
