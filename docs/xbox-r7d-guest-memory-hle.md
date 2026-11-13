# R7D — real coreinit memory operations after observed Xbox startup

## R7C hardware evidence

The owner-provided `nsmbu_r7c_real_entrypoint.txt` confirms:

```
[rpx/R7B] MAIN OSThread READY: addr=60100000 bytes=1696
[rpx/R7] STOP: setup=0 status=2 entry=02AA2E50
    last-pc=021DDDBC lr=021DDCA0
    entered=1947 loops=181 coreinit-handled=2
[rpx/R7] FIRST BLOCKER:
    missing HLE coreinit.OSBlockMove
```

R7C ran 1,947 **function-entry events**, not necessarily 1,947 distinct
functions. The rendering afterwards was still the R5F local PNG preview.

## R7D large step

Implement a *family* of five related real Cafe OS memory operations,
matching `runtime/src/hle/coreinit_mem.cpp` in the desktop runtime:

- `coreinit.OSBlockMove(dest, src, bytes)`: host `memmove`, handles overlap
- `coreinit.OSBlockSet(dest, value, bytes)`: host `memset`
- `coreinit.memcpy(dest, src, bytes)`: host `memcpy`
- `coreinit.memmove(dest, src, bytes)`: host `memmove`
- `coreinit.memset(dest, value, bytes)`: host `memset`

All five preserve the PPC ABI's return value `r3=dest`; `OSBlockMove`
does NOT use memcpy because its source and destination may overlap.

The Xbox guest VM reserves 4 GiB without committing it all. Before
touching memory, the host validates *every affected page* using UWP
`VirtualQuery`, checks access protections and 32-bit address overflow,
and limits each operation to 32 MiB. A missing page, invalid pointer,
oversized operation or protected region stops with a specific blocker,
rather than corrupting memory or claiming success.

The explicit private R7 entrypoint opt-in is still required.
No additional Nintendo code is added to the public repository.

## Owner hardware validation

Owner-only AppX version: **0.9.3.0**.

After installing, read `LocalState/nsmbu_r7d_real_entrypoint.txt`.
Expected evidence of progress:

```
[rpx/R7D] MEMORY HLE: completed=1 last-dst=........ src-or-value=........ len=...
[rpx/R7] FIRST BLOCKER: ... (different from missing HLE coreinit.OSBlockMove)
```

A failed memory span is a valid and useful outcome: it says what part of
the guest address space requires proper heap/system initialization.
Do not treat any synthetic PNG as a real GX2 frame.

## What remains

This is **not** a complete port of Cafe OS. The scheduler, heap manager,
GX2, input, FS and assets are still missing. The next integration follows
the next actual exception rather than adding further audits.
The previous R7C/R6A AppX remains available for rollback; back up
`LocalState` before reinstalling.
