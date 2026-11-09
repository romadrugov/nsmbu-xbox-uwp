# Xbox 0.6.1 — Cafe OS RPX ELF header fix

Previous v0.6.0: Xbox read `LocalState/red-pro2.rpx` (9,095,488 bytes)
but rejected it with `ELF is not executable PowerPC v1`.

Why: Wii U Cafe OS RPX/RPL is a modified big-endian ELF32 PowerPC
executable and uses `e_type=0xFE01`, unlike the public synthetic
`ET_EXEC=0x0002`. The strict loader only accepted the latter.

What changed:

- `rpx_inspect.cpp` accepts `0xFE01` or `0x0002` only,
  still validates PowerPC (`e_machine=20`) and ELF version 1.
- ELF type, machine and OSABI appear in successful metadata reports.
- Local image's **raw ELF header** (type, machine, version, OSABI,
  section table, entry) is logged even if a later section is invalid.
- Local SHA-256 comparison against the existing `recomp/builds.py`
  fingerprints happens **before structural validation**, so the build
  is identified even if the remaining parser cannot inspect it.
- Linux CPU tests mutate a public synthetic ELF32 fixture to reproduce
  Cafe OS `e_type=0xFE01`, with OSABI 0xCA, and reject incorrect types.
- There is no decompression, loading or execution of real game code;
  this patch is metadata-only. No copyrighted game data enters CI.

Expected log after installing `NSMBUXboxSmoke_0.6.1.0_x64.appx`:

```text
NSMBU Xbox RPX Inspection Probe v0.6.1 started
[rpx/0.6] LOCAL image read: 9095488 bytes (not executed)
[rpx/0.6] LOCAL ELF header: class=1 endian=2 osabi=CA type=FE01 ...
[rpx/0.6] LOCAL SHA256 recognized: <build> (metadata only)
[rpx/0.6] LOCAL: ELF type=FE01 machine=0014 ...
[rpx/0.6] LOCAL inspection finished; execution DISABLED
```

The actual OSABI/type/build may differ — the log is authoritative.
If structural validation fails on a different section condition,
retain that failure text for the next targeted patch rather than
assuming that the game itself is invalid.

The existing file should remain in the app's `LocalState` when the
same package identity is updated, but confirm via Xbox Device Portal.
Do not upload a personal copy of `red-pro2.rpx` to GitHub.
