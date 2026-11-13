#!/usr/bin/env python3
"""Private-only R8 bridge for the hook-free USA_V0 generated C.

The R5/R7 owner input was generated without recompiler hooks because the
headless Xbox boot probe intentionally did not implement them. The *full*
NSMBU runtime expects the normal ABI: f_XXXXXXXX -> hook_XXXXXXXX, with
f_XXXXXXXX_orig holding the game-derived function body.

This deterministic bridge constructs that ENTRY wrapper ABI from the
existing owner-private C (outside the public checkout). No game assets
are downloaded, printed or committed. It is an experimental build bridge:
it does not reconstruct instruction-site hooks or undo optimizations made
when the owner initially recompiled with hook_mode=none. For final gameplay,
regenerate from the user's own RPX with the upstream hook-aware recompiler.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
RECOMP = ROOT / "recomp"

# Match only an actual emitted C definition (not a call, declaration,
# generated table reference or instruction comment).
FUNC_DEF = re.compile(
    r"^void f_([0-9A-F]{8})\(Cpu\* __restrict c\) \{[ \t]*$",
    flags=re.MULTILINE,
)
CODE_FILE = re.compile(r"code_[0-9]+\.c\Z")
DECL = re.compile(r"\bvoid f_([0-9A-F]{8})\(Cpu\* __restrict c\);")


def hook_addresses_for(build_name: str) -> tuple[set[str], int]:
    sys.path.insert(0, str(RECOMP))
    import builds

    build = builds.by_name(build_name)
    if build is None or build.name != "USA_V0":
        raise ValueError("R8 bridge only supports the verified USA_V0 build")
    entries, _ = builds.read_hooks(builds.hook_files(), build)
    hooks: set[str] = set()
    skipped_sites = 0
    for site, canonical, address, _, _ in entries:
        if site:
            skipped_sites += 1
            continue
        if build.function_missing(canonical):
            continue
        # Canonical symbol in the emitted native C. The actual guest-PC
        # address may differ from that symbol in base-disc USA_V0.
        if address & 3:
            raise ValueError("Invalid mapped hook alignment")
        hooks.add(f"{canonical:08X}")
    if not hooks:
        raise ValueError("No whole-function hooks for USA_V0")
    return hooks, skipped_sites


def reconstruct(source: Path, destination: Path, hooked: set[str]) -> int:
    source, destination = source.resolve(), destination.resolve()
    if not source.is_dir() or source == destination or source in destination.parents:
        raise ValueError("Separate private source and output folders required")
    if ROOT == destination or ROOT in destination.parents:
        raise ValueError("Owner-derived code cannot be written to public repo")
    if destination.exists():
        raise ValueError("Fresh, non-existing private output directory required")

    inputs = sorted(p for p in source.iterdir() if CODE_FILE.fullmatch(p.name))
    if len(inputs) != 93:
        raise ValueError("Expected exactly 93 owner-private generated code chunks")
    header = source / "funcs.h"
    if not (header.is_file() and (source / "table.c").is_file()
            and (source / "imports.c").is_file()):
        raise ValueError("Missing generated header/table/imports")

    funcs = header.read_text(encoding="utf-8")
    declared = set(DECL.findall(funcs))
    missing = hooked - declared
    if missing:
        raise ValueError(f"Hook targets absent from generated table/header: {len(missing)}")
    if re.search(r"\bvoid f_[0-9A-F]{8}_orig\s*\(", funcs):
        raise ValueError("The source is already hooked; do not apply compatibility bridge")

    # Preflight all 93 code files before creating any output. Never rewrite
    # non-target names, direct calls, or the generated function table.
    occurrences: dict[str, int] = {}
    rewritten: dict[str, str] = {}
    for path in inputs:
        raw = path.read_text(encoding="utf-8")
        matches = list(FUNC_DEF.finditer(raw))
        targets = {m.group(1) for m in matches} & hooked
        for name in targets:
            occurrences[name] = occurrences.get(name, 0) + 1

        def wrap(m: re.Match[str]) -> str:
            addr = m.group(1)
            if addr not in hooked:
                return m.group(0)
            # Matches recomp.py's two generated entrypoint declarations;
            # the original function body (following the opening brace)
            # remains derived from the owner's existing private source.
            return (f"void f_{addr}(Cpu* __restrict c) {{ hook_{addr}(c); }}\n"
                    f"void f_{addr}_orig(Cpu* __restrict c) {{")

        rewritten[path.name] = FUNC_DEF.sub(wrap, raw)
    if any(occurrences.get(name) != 1 for name in hooked):
        bad = sum(occurrences.get(name) != 1 for name in hooked)
        raise ValueError(f"{bad} hook targets do not have exactly one native definition")

    shutil.copytree(source, destination)
    for name, src in rewritten.items():
        if src != (source / name).read_text(encoding="utf-8"):
            (destination / name).write_text(src, encoding="utf-8")
    new_prototypes = [
        "\n/* R8 temporary entry-hook ABI; private hooked recompile preferred. */",
    ]
    for addr in sorted(hooked):
        new_prototypes.append(f"void f_{addr}_orig(Cpu* __restrict c);")
        new_prototypes.append(f"void hook_{addr}(Cpu* c);")
    (destination / "funcs.h").write_text(
        funcs.rstrip() + "\n" + "\n".join(new_prototypes) + "\n",
        encoding="utf-8",
    )
    return len(occurrences)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--build", default="USA_V0")
    args = parser.parse_args()
    try:
        hooks, sites = hook_addresses_for(args.build)
        count = reconstruct(Path(args.generated), Path(args.out), hooks)
    except (OSError, ValueError) as exc:
        parser.exit(1, "R8 hook ABI compatibility stopped: " + str(exc) + "\n")
    print(f"R8 temporary private entry-hook ABI: {count} wrappers; "
          f"{sites} instruction-site hooks NOT reconstructed")
    print("NOT equivalent to proper hook-aware recompilation; not proof of gameplay")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
