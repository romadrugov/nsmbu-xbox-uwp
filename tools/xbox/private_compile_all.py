#!/usr/bin/env python3
"""Compile ALL private NSMBU generated C to Windows/MSVC-ABI COFF and .lib.

This MUST run on a PRIVATE Windows x64 machine with clang-cl and lib.exe.
It NEVER uploads source/objects/logs. Use a fresh --out OUTSIDE checkout.
The --public-fixture flag is ONLY for the authored 3-instruction PPC fixture.

Private example:
 py -3 tools/xbox/private_compile_all.py --generated D:\private\USA_V0\generated --summary D:\private\USA_V0\summary.json --out D:\private\USA_V0\msvc-objects

Then pass OUT/nsmbu_game_msvc.lib to try_private_game_link.ps1.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from private_gamecode import ROOT, inside_repo
from private_compile_one import verify_fixture, verify_private_summary, is_under

CODE = re.compile(r"code_[0-9]+[.]c\Z")


def compile_all(args):
    if os.name != "nt":
        raise ValueError("Windows x64 with VS developer tools is required; cannot use macOS clang-cl")
    if os.getenv("GITHUB_ACTIONS") == "true" and not args.public_fixture:
        raise ValueError("refusing private game code on GitHub Actions")
    gen = Path(args.generated).expanduser().resolve()
    out = Path(args.out).expanduser().resolve()
    if inside_repo(gen) and not args.public_fixture:
        raise ValueError("private generated code must be outside checkout")
    if inside_repo(out) or is_under(out, gen) or is_under(gen, out):
        raise ValueError("compiled private output must be outside checkout and generated directory")
    if out.exists() and any(out.iterdir()):
        raise ValueError("output directory must be new or empty")
    if args.public_fixture:
        verify_fixture(gen)
        expected = 1
    else:
        verify_private_summary(gen, args.summary)
        expected = 93
    sources = sorted((p for p in gen.glob("code_*.c") if CODE.fullmatch(p.name)), key=lambda p: p.name)
    if len(sources) != expected:
        raise ValueError(f"expected {expected} generated chunks, found {len(sources)}")
    table = gen / "table.c"
    if not table.is_file():
        raise ValueError("generated table.c missing")
    sources.append(table)
    imports = gen / "imports.c"
    if imports.is_file():
        sources.append(imports)
    elif not args.public_fixture:
        raise ValueError("private generated imports.c missing")
    clang = shutil.which("clang-cl")
    libexe = shutil.which("lib")
    if not clang or not libexe:
        raise ValueError("open x64 VS Developer PowerShell with clang-cl and lib.exe on PATH")
    out.mkdir(parents=True, exist_ok=False)
    objs = out / "objects"
    logs = out / "logs"
    objs.mkdir()
    logs.mkdir()
    built = []
    for i, src in enumerate(sources, 1):
        target = objs / (src.stem + ".obj")
        cmd = [clang, "/nologo", "/c", "/Od", "/MD", "/clang:-std=c11",
               "/DWINAPI_FAMILY=WINAPI_FAMILY_APP",
               "/I" + str(gen), "/I" + str(ROOT / "runtime" / "include"),
               "/Fo" + str(target), str(src)]
        with (logs / (src.stem + ".log")).open("w", encoding="utf-8", errors="replace") as fd:
            status = subprocess.run(cmd, stdout=fd, stderr=subprocess.STDOUT, check=False)
        if status.returncode != 0:
            raise RuntimeError(f"clang-cl stopped at {src.name}; inspect PRIVATE logs/{src.stem}.log")
        if not target.is_file() or target.read_bytes()[:2] != b"\x64\x86":
            raise RuntimeError(f"expected Windows AMD64 COFF from {src.name}")
        built.append(target)
        print(f"[{i}/{len(sources)}] {src.name}: AMD64 COFF PASS", flush=True)
    library = out / ("public_fixture_msvc.lib" if args.public_fixture else "nsmbu_game_msvc.lib")
    cmd = [libexe, "/nologo", "/MACHINE:X64", "/OUT:" + str(library)] + [str(p) for p in built]
    with (logs / "library.log").open("w", encoding="utf-8", errors="replace") as fd:
        status = subprocess.run(cmd, stdout=fd, stderr=subprocess.STDOUT, check=False)
    if status.returncode or not library.is_file():
        raise RuntimeError("MSVC lib.exe failed, inspect PRIVATE logs/library.log")
    report = {
        "schema": "nsmbu-private-all-coff-v1",
        "mode": "PUBLIC_FIXTURE" if args.public_fixture else "USA_V0",
        "chunks": expected,
        "object_count": len(built),
        "archive_bytes": library.stat().st_size,
        "coff_machine": "0x8664",
        "linked_uwp_x64_executable": False,
        "game_code_executed": False,
        "playable": False,
        "private_output_only": True,
    }
    (out / "compile-summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("MSVC-compatible static archive:", library, flush=True)
    print("Safe metadata: compile-summary.json; never upload .lib, .obj or compiler logs.")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--generated", required=True)
    p.add_argument("--summary")
    p.add_argument("--out", required=True)
    p.add_argument("--public-fixture", action="store_true")
    a = p.parse_args()
    try:
        compile_all(a)
    except (ValueError, OSError, RuntimeError) as e:
        p.exit(1, "Private recompilation stopped: " + str(e) + "\n")


if __name__ == "__main__":
    main()
