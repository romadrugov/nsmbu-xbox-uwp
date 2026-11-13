# R7B — fix the first actual NSMBU startup blocker: coreinit.OSGetCurrentThread

The owner's R7 Series S log recorded the first true execution blocker:

\`\`\`
[rpx/R7] STOP: setup=0 status=2 entry=02AA2E50 last-pc=02AA2E50 entered=1 loops=0
[rpx/R7] FIRST BLOCKER: reason=Cafe OS HLE at 02AA2E50: missing HLE coreinit.OSGetCurrentThread
\`\`\`

## What R7B adds

This is a fix for a **real observed game startup dependency**, not another
metadata-only test. The target remains the real entrypoint 0x02AA2E50.

After the exact USA_V0 SHA, RPX staging and R5B CPU/RELA checks succeed, the
private UWP host commits 64 KiB of guest memory at 0x60100000, outside the
preexisting 0x60000000–0x60100000 guest main stack. It then writes one
0x6A0-byte **guest-backed OSThread structure** matching the offsets in
upstream runtime/src/core/threads.cpp::create_thread:

- OSContext magic at +0/+4; core-1 affinity at +0x304
- thread state READY, attribute 2, ID 1, suspension 0
- effective/base priority 16, stack endpoints and real entrypoint
- zeroed thread-specific slots and remaining fields

It implements only three basic coreinit imports through the owner-private
R7 fail-closed path:

- \`OSGetCurrentThread\` returns the **address of that real guest structure**,
  never a fake null result.
- \`OSGetCoreId\` reports \`Cpu.core\`, matching the upstream service.
- \`OSMemoryBarrier\` issues an actual sequentially consistent CPU fence.

A counter records precisely how many of those calls were serviced.
Every other unsupported Cafe OS call still reports a FIRST BLOCKER rather
than a fake success. There is **still no scheduler**, no heap service, and
no GX2-generated frame.

## Build / output / hardware check

Public fixture CI compiles/links this code with the R7 entry opt-in but
cannot prove runtime behavior with private commercial data. The owner-private
build only pins a validated, immutable public SHA and requires
\`enable_r7_entry_probe=true\`, the matching owner-provided USA_V0 release,
and the signed AppX opt-in.

Install experimental AppX version **0.9.1.0**. Retain the existing
\`LocalState/red-pro2.rpx\` and \`LocalState/nsmbu_preview.png\`. Read
\`LocalState/nsmbu_r7b_real_entrypoint.txt\`.

Expected success at this *specific* stage:
\`\`\`
[rpx/R7B] MAIN OSThread READY: addr=60100000 ...
[rpx/R7] STOP: ... coreinit-handled=1 ...
[rpx/R7] FIRST BLOCKER: ... (not OSGetCurrentThread)
\`\`\`

If the game hits a memory fault or another missing HLE, that is the next
concrete integration task. R7B is not playable Mario and does not fix all
imports. If the new test fails to compile or freezes, revert to verified R7
or R6A AppX and preserve LocalState.
