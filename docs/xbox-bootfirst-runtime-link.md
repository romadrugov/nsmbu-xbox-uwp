# Xbox boot-first diagnostic runtime link

This experimental UWP x64 linker target accepts the existing hook-free USA_V0
recompilation from the private Windows CI **without running the game**.

## Actual components

- \`runtime/src/core/headless_dispatch.c\`: previously validated native
  indexed PowerPC-to-C function and import dispatcher; first-boot
  \`nsmbu_bootfirst_init_dispatch\` loads the generated function table
- \`runtime/src/core/espresso_fp.c\`: actual PowerPC reciprocal and reciprocal
  square-root functions
- \`xbox/runtime-compat/bootfirst_runtime.c\`: bounded trace collection and
  explicit fail-fast diagnostics for PPC traps, unimplemented imports, and
  unavailable thread preemption

The unsupported Cafe OS and scheduler operations deliberately abort. They do
NOT return fabricated success values and do not emulate a working scheduler.
No \`hook_*\` or \`site_*\` functions are stubbed. This is a link/diagnostic
milestone, not a production game runtime.

## Validation gates

The PUBLIC Xbox preflight compiles all three source units with clang-cl,
forces their symbols into the x64 Windows Store link, and confirms a public
synthetic recompilation table can be linked. It contains no Nintendo source.
The PRIVATE GitHub Actions workflow remains the only consumer of the complete
owner-provided USA_V0 game archive. Change its pinned PUBLIC SHA only after
the public preflight succeeds; never inject game assets in the public repo.

Even if a full private link succeeds, an EXE produced by
\`NSMBURuntimeLinkProbe.vcxproj\` is **not** an installable/signable AppX and
does not initialize the Wii U memory map or start the game entry function.
Real first boot requires threading, import mapping, HLE and assets.

## Next engineering work

Replace the diagnostic scheduler and missing-HLE handler with progressively
working upstream implementations and an Xbox VM. Add a separate Xbox boot
harness and record first real game entrypoint or failure with safe diagnostics.
Do not label this stage playable until controller, resources, audio and GX2
rendering work on the actual Series S.
