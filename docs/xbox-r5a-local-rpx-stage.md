# R5A — Xbox local RPX staging preflight (game execution disabled)

R4.1 is preserved at branch `xbox-r4.1-preflight-pass-20261112` and confirmed
on an actual Xbox Series S: fixed 4-GiB guest address range, two sparse memory
page tests, native dispatch table with 84,231 game functions and 2,821
imports, `entry=02AA2E50 resolved=1 status=0`.

R5A combines **that** native full-game UWP shell with the existing,
previously tested `xbox/smoke/RpxProbe.cpp` and
`runtime/src/core/rpx_inspect.cpp` / `rpx_stage.cpp`. It reuses the
same owner-local RPX fingerprint verification, bounded decompression,
overlap checks, sparse guest staging and CRC/readback from earlier smoke
tests, but does not execute a game function.

## On Xbox (after successful private R5A AppX assembly)

1. Install the new private `NSMBUBootPreflight_R4_x64.appx` as an update
   to the existing R4 app (AppX version 0.2.0.0; the name of the package
   filename is retained for CI compatibility). Its signed certificate and
   `Microsoft.VCLibs.x64.14.00.appx` come from the private artifact.
2. From Xbox Device Portal **File Explorer**, select the app
   `Romadrugov.NSMBUXboxBootPreflight` and its **LocalState** directory.
   Copy your own `red-pro2.rpx` there as `red-pro2.rpx`. Do **not**
   add that file to the public repo or to the AppX.
3. Relaunch the diagnostic app and retrieve
   `LocalState/nsmbu_r5_stage_preflight.txt`.
4. The log must show R4 native dispatch `status=0 resolved=1`, a matching
   `USA_V0` SHA fingerprint, valid ELF section inventory and
   `LOCAL section mapping PASSED`. If it fails, do not attempt execution;
   use the specific reason in the local log.

This R5A app uses a byte-accurate SHA-256 build guard. It refuses to stage
any other named revision, even if the independent RPX inspector recognizes
that revision. No original file bytes are printed or sent to CI. The
public AppX test embeds only the openly authored synthetic ELF from
`recomp/android_fixture.py`; the real RPX remains in the owner's
Xbox LocalState storage.

## Limitations

- R5A does not initialize a Wii U scheduler, set up an executable
  `Cpu` stack/register context, relocate imports, or call game entry.
- A successful stage is NOT evidence that the game can boot yet.
- The existing RPX staging code selects only safe allocatable sections
  such as `.text`, `.data`, `.rodata` and `.bss`. Future work must
  confirm full guest memory and relocations before first guest execution.
- The unchanged R3 and R4.1 checkpoints remain usable if R5A fails.
