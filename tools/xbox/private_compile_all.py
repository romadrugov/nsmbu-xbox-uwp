#!/usr/bin/env python3
r"""Compile ALL private NSMBU generated C to Windows/MSVC-ABI COFF and .lib.

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
        # No game-derived input is permitted in public CI. Allow a deliberately
        # opted-in, owner-controlled PRIVATE build repo only.
        trusted_private_ci = (
            os.getenv("GITHUB_REPOSITORY") == "romadrugov/mkwii-xbox-build"
            and os.getenv("NSMBU_PRIVATE_CI_OPT_IN") == "YES"
        )
        if not trusted_private_ci:
            raise ValueError("private game code forbidden outside explicitly opted-in private CI")
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
    libexe = shutil.which("lib.exe") or shutil.which("lib")
    if not libexe:
        # GitHub's Windows runners install MSVC but do not always put lib.exe
        # on PATH. VS Developer PowerShell already exposes it; otherwise
        # resolve the actual installed toolset without guessing its version.
        program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
        vswhere = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        if vswhere.is_file():
            result = subprocess.run(
                [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
                capture_output=True, text=True, check=False,
            )
            if result.returncode == 0 and result.stdout.strip():
                vc_tools = Path(result.stdout.strip()) / "VC" / "Tools" / "MSVC"
                if vc_tools.is_dir():
                    for version in sorted(vc_tools.iterdir(), reverse=True):
                        candidate = version / "bin" / "Hostx64" / "x64" / "lib.exe"
                        if candidate.is_file():
                            libexe = str(candidate)
                            break
    if not clang or not libexe:
        raise ValueError("requires clang-cl and MSVC x64 lib.exe (VS2022 C++ tools)")
    out.mkdir(parents=True, exist_ok=True)
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
        if args.r7_guest_budget:
            # Applies only to owner-private opt-in or original PUBLIC fixture,
            # never to ordinary desktop/game builds.
            cmd.insert(6, "/DNSMBU_R7_GUEST_BUDGET=1")
        with (logs / (src.stem + ".log")).open("w", encoding="utf-8", errors="replace") as fd:
            status = subprocess.run(cmd, stdout=fd, stderr=subprocess.STDOUT, check=False)
        if status.returncode != 0:
            raise RuntimeError(f"clang-cl stopped at {src.name}; inspect PRIVATE logs/{src.stem}.log")
        if not target.is_file():
            raise RuntimeError(f"compiler output missing for {src.name}")
        with target.open("rb") as stream:
            machine = stream.read(2)
        if machine != b"\x64\x86":
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
        "r7_guest_budget_instrumented": bool(args.r7_guest_budget),
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
    p.add_argument("--r7-guest-budget", action="store_true",
                   help="private R7: instrument translated PPC_LOOP callbacks")
    a = p.parse_args()
    try:
        compile_all(a)
    except (ValueError, OSError, RuntimeError) as e:
        p.exit(1, "Private recompilation stopped: " + str(e) + "\n")


if __name__ == "__main__":
    main()
