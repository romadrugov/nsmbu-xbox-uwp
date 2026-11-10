# Xbox 0.7.0 — safe RPX section staging (USA_V0)

Verified baseline: Wii U \`red-pro2.rpx\` in Xbox App LocalState was
recognized as \`USA_V0\` by exact SHA-256 in v0.6.2. The physical-console
log showed 33 ELF sections, 27 allocated, 27 zlib-compressed; entry
\`0x02AA2E50\`, \`.text\` at \`0x02000020\` with decoded size 11,182,212 B.

## What v0.7.0 adds

- A portable decoder \`runtime/src/core/rpx_inspect.cpp::decode_section\`
  accepts uncompressed RPX data, compressed CafeOS RPX data
  (big-endian 32-bit size plus zlib), or SHT_NOBITS as zero-filled memory.
  Size caps: 32 MiB per decoded section.
- \`rpx_stage.cpp\` selects only allocatable \`.text\`, \`.rodata\`,
  \`.data\`, \`.bss\`, and a small standard \`.sdata/.sbss\` family.
  Every section must have a valid size, type, and guest 4GiB range;
  the combined staged budget is 48 MiB. All chosen sections must be
  non-overlapping by byte address. Staging fails atomically before guest
  memory modifications.
- Only a *known SHA256-matched* RPX is allowed to proceed past inspection.
  Unknown or malformed game images are skipped.
- Once every selected section has been decoded to temporary CPU buffers,
  \`GuestVm\` commits page-aligned sparse guest memory at the original
  Wii U addresses. Content is copied, CRC32 checked, and compared byte
  for byte; a NOBITS section is explicitly zero-filled.
- The existing public synthetic PPC fixture and the game's UWP D3D12
  smoke graphics continue to operate as in earlier builds. The app
  does NOT execute or jump to any code from the local RPX.

## Linux CI

A standalone, authored public CafeOS ELF test has a zlib-compressed
12-byte \`.text\`, raw \`.rodata\`/\`.data\`, and NOBITS \`.bss\`.
The host test also injects corrupt zlib/Adler32, overlapping guest
addresses, oversized sections and missing text. It checks CRC32,
exact byte content and unchanged output on failed preparations.

## Test on your Xbox Series S

1. Download the \`nsmbu-xbox-smoke\` artifact from the v0.7.0 GitHub Actions
   run, and update the existing signed AppX to
   \`NSMBUXboxSmoke_0.7.0.0_x64.appx\`.
2. Keep \`LocalState/red-pro2.rpx\` on the console. Do NOT upload it to
   GitHub or put it in any CI artifact.
3. On launch, \`nsmbu_xbox_diag.txt\` should still show
   \`LOCAL SHA256 recognized: USA_V0\`, plus:
   \`\`\`
   [rpx/0.7] Phase 1: decode eligible sections to CPU buffers (no guest writes)
   [rpx/0.7] staging VALIDATED: sections=... bytes=..., no overlaps
   [rpx/0.7] Phase 2: commit sparse guest pages and copy checked bytes
   [rpx/0.7] .text: guest=02000020 bytes=11182212 crc32=... VERIFIED
   [rpx/0.7] LOCAL section mapping PASSED: .../... sections, ... bytes
   [rpx/0.7] LOCAL game execution DISABLED: no entry-point jump, no relocations/HLE
   \`\`\`
   Exact staged sections will depend on the game's ELF metadata. If
   any section fails decoding or size/overlap checks, report that exact
   failure; the guest mapping step should not begin.
4. The previously tested checkerboard and gamepad A behavior should
   remain unchanged.

Important limitations: the RPX is *not* relocatable, imports are not
resolved, page protections are RW during the test, Wii U OS/HLE remains
incomplete, and the actual game's recompilation output is not linked
into the UWP app. This is **guest memory preparation**, not running Mario.

Rollback build: \`xbox-uwp-stable-0.6.2\`.
