# R5E — make Xbox guest-memory loading match the existing desktop runtime

The owner's Xbox Series S R5D.1 log successfully decoded 532,197 RELA
records without structural errors. Of 3,306 imported-symbol references, 3,280
are REL24 branch candidates and 26 are imported text immediate rewrites.
The recompiler `recomp/recomp.py::_imm_overrides()` already handles a
subset of the latter. The existing desktop runtime `core.cpp::load_rpx`
**does not reapply all ELF relocations on boot**. We must not blindly
patch all 532K records into the staged Wii U image.

Instead, compare the concrete loader behavior:

- The desktop loader copies all allocated RPX sections at guest addresses
  [0x02000000, 0xC0000000), including the owner's two missing boot sections
  `.syscall` (0x02000000, 8 bytes) and `.module_id` (0x101C9660, 56 bytes).
- Before the copies it zeros the contiguous MEM2 span from its first
  allocatable section to the last. Xbox R5A–R5D only sparsely committed the
  four named sections, not the whole initialized MEM2 interval.

R5E adds an **isolated, opt-in** owner-local loading mode:

1. The original SHA256/USA_V0 and entrypoint guard still applies.
2. `prepare_sections(..., includeBootSections=true)` also decodes only the
   two named standard SHT_PROGBITS boot sections, preserving checksum and
   overlap checks. Earlier public smoke/default mode still selects 4/4
   sections, unchanged.
3. `boot_mem2_span()` calculates a bounded (<=32MiB) contiguous range
   of previously decoded MEM2 sections; the R5E Xbox app commits and
   zero-fills that range **before** copying the real data sections.
4. CRC + exact readback still verify all staged sections. On the owner
   build we expect six sections, including `.syscall` and `.module_id`.
   This is a *prediction*, not a validated console result yet.
5. R5B CPU prep and R5C/R5D import/RELA audits continue after successful
   staging; all original game code execution remains disabled.

The AppX version increases to **0.6.0.0**, output log:
`LocalState/nsmbu_r5e_boot_sections.txt`. The owner-local
`red-pro2.rpx` stays in Xbox LocalState; no commercial game bytes are
committed or uploaded to public CI.

## Key caveat

R5E intentionally does NOT map unbounded import tables at 0xC0..., execute
Cafe OS functions or claim that the r13 signed-16-bit window is available.
It does not silently stub unimplemented services. We still need real
`init_data_imports`, `mem_setup_heaps`, a scheduler and imported Cafe OS
function implementations before unbounded gameplay entry.
