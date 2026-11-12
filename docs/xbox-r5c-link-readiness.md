# R5C Link and Guest Memory Readiness (game execution disabled)

R5B verified on physical Xbox Series S with the owner-local USA_V0 RPX:
SHA matched, all four selected data sections staged and CRC-verified,
native PPC Cpu state initialized:
entry 0x02AA2E50, guest stack 0x60000000..0x60100000, r2 0x10008000
and r13 0x10231120.

## Important finding

The staged .bss ends at 0x10229104, while the actual r13 SDA base is
0x10231120 (32,796 bytes higher). r13 need not be dereferenced on its own;
it is a base for signed-offset addressing. But that base is not covered by
any of the four staged sections, and an immediate first game call is
not a defensible next step without auditing and preparing the missing
memory and Cafe OS services.

## R5C diagnostics

- Runs exactly the existing R4/R5A/R5B checks, and never calls game code.
- Reads the native `g_recomp_imports` table and reports counts of function
  and data imports, non-null pointers, registered dispatch targets, and
  designated 0xC1000000-series virtual data addresses.
- Explicitly warns that generated `imports.c` contains *weak*
  `hle_unimplemented` wrappers. A non-null function pointer is **not**
  implementation of a Cafe OS service.
- Counts all RPX allocated sections, only sections actually staged, the
  candidate relocations (ELF SHT_REL / SHT_RELA or `.rel*` names),
  and logs metadata for every allocated-but-un-staged section.
- Checks whether entry, r2 and r13 lie in one of the verified staged
  sections. This is conservative: uncovered SDA base is a warning,
  not proof that the final game would dereference that specific byte.
- Never loads, executes or sends original Nintendo section bytes to GitHub.
- Uses `NSMBU_R5C_LINK_AUDIT` to isolate the new report from old smoke.
  New private AppX version 0.4.0.0, local report
  `LocalState/nsmbu_r5c_link_readiness.txt`.

## Safety and next milestone

Do not treat R5C as executing game code. It does not perform relocations,
resolve Cafe OS host services, initialize a scheduler, or map virtual
data-import slots. A safe R5D/first-entry milestone requires replacing
weak HLE defaults with real service implementations, mapping the needed
guest regions, and guarding first execution against unsupported functions.
The R5B working build is pinned separately.
