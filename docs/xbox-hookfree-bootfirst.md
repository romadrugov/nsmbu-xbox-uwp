# Xbox boot-first: hook-free private USA_V0 recompilation

Goal: isolate original game execution from desktop-only hook replacements so that
the first Xbox runtime integration can focus on dispatch, timing, memory, Cafe OS
and loading. This is **not** an executable Xbox port or a shortcut around those
runtime dependencies.

## Changes

The normal recompiler keeps reading `recomp/hooks.txt` and `hooks_*.txt`.
Setting `NSMBU_RECOMP_DISABLE_HOOKS=1` makes `builds.hook_files()` return no
manifests for that process. The local `private_gamecode.py prepare` command
exposes this as `--hooks none` and writes `"hook_mode": "none"` to its private
summary. The existing default is unchanged.

No hooks are silently mapped to no-ops. The recompiler must generate the
original function bodies instead. It may emit a different source and object
inventory from the regular 93-file version; verify it rather than assuming.

## Mac: prepare a separate new directory (no upload)

After checking out this feature branch on the Mac:

```sh
python3 tools/xbox/private_gamecode.py prepare \
  --rpx "$HOME/Downloads/NSMBU-extracted/code/red-pro2.rpx" \
  --out "$HOME/NSMBU-Xbox-private/USA_V0_BOOTFIRST_NOHOOKS" \
  --hooks none --jobs 4
```

The output directory MUST be new and outside the public checkout. Do not
modify or overwrite `USA_V0` or the 34-MB original private Release archive.
Do not upload the RPX, generated sources or archives to the public repository.

Compare the new `summary.json` and local `recompile.log` counts with the
original. A second private Release or CI input must be explicitly configured
separately to test the new archive on GitHub's Windows runner; the current
`nsmbu-usa-v0-inputs-01` tag refers to the existing hooked recompilation.

## What this does NOT do

- Does not link core dispatch, scheduler, tracing, FPU, Cafe OS or graphics
- Does not prove a Windows Store x64 EXE or Xbox AppX is playable
- May require reintroducing selected compatibility hooks once boot behavior
  is understood; never assume all optional hooks are unnecessary to play
- Does not change the existing verified R2.3/R2.4 console results
