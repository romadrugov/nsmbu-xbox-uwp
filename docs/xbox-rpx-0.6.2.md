# Xbox 0.6.2 — compressed Wii U RPX section-name table

## What we learned from the physical Xbox

The user-provided, privately stored `red-pro2.rpx` has:

- Exact SHA-256 match for the existing `USA_V0` build
- 9,095,488 bytes
- Cafe OS ELF32 big-endian PowerPC, e_type `0xFE01`, OSABI `0xCA`
- Entry point `0x02AA2E50`
- 33 section headers, section-name string table index 30

v0.6.1 was rejecting the RPX at **compressed section-name string
table**. The previous parser unconditionally rejected the
`SHF_RPL_ZLIB` flag on `.shstrtab`.

## What changed

- Bundled portable `sinfl.h` (Micha Mettke, MIT or public domain),
  under `runtime/src/core/third_party/sinfl.h`.
- Added narrow, bounded RFC1950 zlib decompression for **only the
  section-name table**, using Cafe OS's four-byte big-endian original
  size prefix, followed by the compressed stream.
- Validates zlib method/header and Adler-32 checksum, enforces a maximum
  decoded size of 1 MiB, checks section-name NUL termination, and
  rejects truncated/corrupted/oversized tables.
- Other game sections remain metadata-only; this patch does **not**
  decompress executable code, copy Nintendo instructions into guest
  memory, resolve imports or execute game code.
- Added deterministic public synthetic compressed-shstrtab unit tests,
  including failures on malformed zlib headers/checksum, truncated data,
  oversized claimed expansion.
- Reports compressed/uncompressed shstrtab state and decoded byte count.

## Test with the existing local RPX

Install the GitHub Actions `NSMBUXboxSmoke_0.6.2.0_x64.appx` from
`xbox-uwp`. The user's `LocalState/red-pro2.rpx` can stay on the
console if the package update preserves app data.

Expected lines (actual counts depend on the RPX):

```text
NSMBU Xbox RPX Inspection Probe v0.6.2 started
[rpx/0.6] LOCAL image read: 9095488 bytes (not executed)
[rpx/0.6] LOCAL SHA256 recognized: USA_V0 (metadata only)
[rpx/0.6] LOCAL section-name table: zlib-compressed, decoded-bytes=...
[rpx/0.6] LOCAL .text: address=... file-bytes=... decoded-bytes=... compressed=...
[rpx/0.6] LOCAL inspection finished; execution DISABLED
```

A new structural error may appear after successfully decoding section
names; in that case use the exact error message to determine the next
minimal fix. Do not assume the title has started or that compressed
game sections can be executed yet.

The previous Xbox-tested stable build remains 0.6.1; 0.6.2 needs a
physical-console smoke test.
