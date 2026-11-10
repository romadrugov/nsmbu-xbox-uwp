# Xbox 0.8.0 — private local USA_V0 game-code preparation

## Current state, verified on a physical Xbox Series S

0.7.0 successfully decoded and mapped the user's own RPX sections
(.text, .rodata, .data and .bss) into guest memory, with CRC32 and
byte-for-byte comparisons. The matching RPX build is USA_V0.
This does **not** mean the Xbox has executed any Nintendo game code.

The current CI builds a public, authored, three-instruction PowerPC
test (r3=42) and compiles the generated C with clang-cl for Xbox UWP.
That only validates the compiler/guest dispatch ABI on a tiny fixture.

## Why this milestone is local-only

The actual USA_V0 recompilation produces many code_NNN.c files,
a table.c guest-address dispatch table, funcs.h and imports.json.
These are derived from the user's legally obtained game copy. They
must NOT be uploaded to the public GitHub repository, GitHub Issues,
Actions logs, release assets or chat attachments.

We also cannot take a pre-built macOS executable and run it on Xbox:
macOS ARM64 Mach-O and Xbox Windows x64 UWP have different ABIs,
binary formats and runtime dependencies.

## On the Mac

1. Clone/check out branch xbox-uwp of the public repository.
2. Keep the already extracted red-pro2.rpx locally. A typical path is
   ~/Downloads/NSMBU-extracted/code/red-pro2.rpx.
3. Run the following commands in Terminal:

    git clone --branch xbox-uwp https://github.com/romadrugov/nsmbu-xbox-uwp.git
    cd nsmbu-xbox-uwp
    python3 tools/xbox/private_gamecode.py prepare \
      --rpx "$HOME/Downloads/NSMBU-extracted/code/red-pro2.rpx" \
      --out "$HOME/NSMBU-Xbox-private/USA_V0" \
      --jobs 4

Use an alternate local --rpx path if needed. The script checks the
RPX's SHA-256 against the known build registry and REFUSES to proceed
unless it is exactly USA_V0. It calls the upstream project recompiler
with NSMBU_RECOMP_MOD_HOOKS=0 (same as the installer).

The output is intentionally outside the Git checkout:
- ~/NSMBU-Xbox-private/USA_V0/generated/code_NNN.c
- ~/NSMBU-Xbox-private/USA_V0/generated/funcs.h
- ~/NSMBU-Xbox-private/USA_V0/generated/table.c
- ~/NSMBU-Xbox-private/USA_V0/generated/imports.json
- ~/NSMBU-Xbox-private/USA_V0/recompile.log
- ~/NSMBU-Xbox-private/USA_V0/summary.json

If any part fails, inspect recompile.log locally. The tool does not
network, copy, upload or package the RPX. It only reads it to verify
build identity and invokes the locally checked-out recompiler.

## What is safe to share

The summary.json contains only the build ID, known RPX fingerprint,
entry address, function/import counts, number/size of generated C files,
and explicit false values for native_xbox_linked and
game_code_executed_on_xbox. You may share the summary JSON text.
Do **not** share any generated .c/.h or the log with source snippets.

To inspect an existing public or private generated folder without
verifying that it came from the game's RPX, use:

    python3 tools/xbox/private_gamecode.py inspect --generated "PATH"

This command marks its output as UNVERIFIED. Only the prepare command
confirms the actual USA_V0 hash.

## Next integration gate

To test an actual game function, we must first identify a suitable
function in the locally generated code, understand its register, guest
memory, system import and side-effect dependencies, and compile it for
Windows x64 UWP with a compatible Clang toolchain. The current macOS
build toolchain cannot by itself link a signed Windows UWP AppX.
Options include a locally managed Windows build environment or a
private/self-hosted Windows runner that has access to those private
generated sources. **Never use public GitHub Actions to upload or
artifact the generated game code.**

There is deliberately NO Xbox 0.8.0 AppX yet: a new synthetic-only
binary would not satisfy the goal of executing a real Mario function.
The physical-console rollback branch is xbox-uwp-stable-0.7.0.
