# R5D — first-entry dependency preparation (read-only ELF RELA audit)

R5C was verified on the owner's physical Series S. Native dispatch registered
84,231 game functions and 2,821 imports (2,776 function wrappers, 45 virtual
data slots); 4 RPX sections and the real PPC Cpu bootstrap passed. The R5C
log found 3 relocation candidate sections (2,353,023 file bytes) and the
SDA base r13=0x10231120 outside the four staged sections.

## Important distinctions

The 23 unstaged ELF allocated sections are **not 23 missing runtime regions**:
most of the size is the .symtab plus import and other ELF metadata. It would
be incorrect to map all 23 unconditionally. Equally, r13 is a base for signed
16-bit relative loads/stores, not a promise that the byte at r13 itself will
be read. The R5D report computes how many bytes of the actual +/-32 KiB
*address window* intersect our staged ranges. This is a conservative
coverage inventory, not an address-access trace.

The recompiler already processes import-relocation data when emitting C:
`recomp/recomp.py::_imm_overrides` recognizes imported .text
`R_PPC_ADDR16_LO/HI/HA` and converts targets to mapped virtual data
addresses. **Do not apply all raw RPX RELA relocations again** to native
generated functions or the staged image: that risks double-relocating
addresses and corrupting an otherwise valid runtime.

## What R5D actually does

- Only after local USA_V0 SHA fingerprint verification, 4/4 section staging,
  and PPC Cpu bootstrap, decode the RPX ELF32 RELA and symbol table sections
  using the already bounded `rpx_inspect.cpp` decoder.
- Preserve `sh_link`, `sh_info` and `sh_entsize` to find each relocation's
  target section and linked symbol table.
- Decode each 12-byte big-endian RELA record, bounded to 512,000 records.
  Count PPC types `ADDR32=1`, `ADDR16_LO=4`, `ADDR16_HI=5`,
  `ADDR16_HA=6`, `REL24=10`, import-symbol references, relocation offsets
  in the first 256 bytes of the game's entry function, and invalid indices.
- Determine whether the relocation symbol belongs to a Cafe OS import
  section (`SHT_RPL_IMPORTS=0x80000002`). Count imported .text immediate
  rewrites already considered by the recompilation pass and imported branch
  candidates for later review. This does **not** establish actual HLE support.
- Report per-RELA-section counts without printing source bytes, function
  bodies, symbol names, offsets of individual relocations or content hashes.
- Explicitly keep **game execution and relocation patching disabled**.
- New AppX version 0.5.0.0, same package identity; log:
  `LocalState/nsmbu_r5d_relocation_audit.txt`.
- Synthetic parser unit test covers valid imports, out-of-bounds symbol/target,
  corrupt metadata, and signed SDA window overlap. The public Xbox fixture
  AppX must compile, link and sign before the pin in private CI advances.

## What this cannot tell us

The report cannot identify all operations that Mario will perform during its
entry function, or prove whether any particular Cafe OS service is implemented.
The first *native* game instruction has not yet run. The following work should
use the counted relocation types and import references to define a deliberate
single-entry execution boundary with instruction budget and fail-fast services.
Avoid a blind direct function call that could loop indefinitely inside one
recompiled function.

The owner-private archive remains in the private GitHub repository; the actual
RPX stays only in Xbox LocalState. Public CI uses only a handcrafted synthetic
PPC ELF/RPX.
