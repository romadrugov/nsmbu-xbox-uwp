# Xbox R5B: PPC CPU bootstrap preflight (no guest execution)

R5A was verified on the physical Xbox Series S: local SHA256-confirmed
USA_V0 RPX, `.text`, `.rodata`, `.data`, `.bss` all staged, 13,447,388
bytes checked by CRC/readback. The native 84,231-function dispatcher resolves
entry `02AA2E50`; **none of those game functions have been called**.

## R5B changes

- After R5A's strict local RPX SHA and 4/4 section staging success, extract
  PPC startup metadata from the real Cafe OS ELF section of type
  `0x80000004`, at byte offsets 0x24/0x28/0x2C. These are the same SDA,
  SDA2 and stack fields the upstream `load_rpx` / `threads::run_main` use.
- Refuse missing, duplicate, truncated, incompatible or unsafe metadata.
  Runtime stack size = maximum of requested and 1 MiB, capped at 16 MiB.
- Commit and zero a bounded stack at guest `0x60000000`, outside the loaded
  `.text`/MEM2 data. Use the actual `Cpu` ABI from `runtime/include/ppc.h`,
  core 1, `r1=(stack_top-0x20)&~15`, `r2=SDA2`, `r13=SDA`,
  `pc=entry`, `lr=0`, `r3=argc=0`, `r4=argv=0`.
- Log CPU snapshot without copying RPX bytes or disclosing any original
  private code. No generated PPC function is invoked. No fake-success HLE.
- Bump standalone diagnostic AppX version to `0.3.0.0`, log filename:
  `LocalState/nsmbu_r5b_cpu_bootstrap.txt`.

The existing smoke RpxProbe is preserved: the CPU-bootstrap code only
compiles when `NSMBU_R5_CPU_BOOTSTRAP=1`, set by the dedicated boot project.
The new parser has handcrafted synthetic C++ unit tests; public Windows CI
also verifies the complete R5B WinRT x64 AppX link and signing against a
non-Nintendo synthetic PPC fixture.

## Critical limitations

This is **not first guest instruction yet**: we still have no validated
Cafe OS relocations, import binding, system thread scheduler, alarm/timebase,
or game-owned memory services. Directly calling `entry` now could access
uncommitted guest pages and terminate the app. These must be diagnosed and
handled before a controlled first-execution test. The R4.1 and R5A
checkpoints remain untouched.

## On the user's Xbox (after a green private R5B build)

- The existing owner-local `LocalState/red-pro2.rpx` stays on the console
  and is reused. Do not upload it to public GitHub.
- Install the signed private AppX `0.3.0.0` as an update, along with its
  certificate and VCLibs dependency as necessary.
- Read `LocalState/nsmbu_r5b_cpu_bootstrap.txt` and verify R5A 4/4 staging,
  plus `[rpx/R5B] CPU BOOTSTRAP PASS` and `status=0`.
- If CPU bootstrap fails, inspect the specific metadata or stack error.
  Never attempt to run game code in the diagnostic app while red.
