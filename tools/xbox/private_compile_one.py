#!/usr/bin/env python3
"""Prepare/compile ONE private generated PowerPC-to-C chunk as Win64 COFF.

This is a local developer tool, NOT a game binary or a complete UWP build.
Private code and compiler logs must never be uploaded to public CI.

On a Mac:    python3 tools/xbox/private_compile_one.py plan --generated DIR --summary SUMMARY
On Windows:  py -3 tools/xbox/private_compile_one.py compile --generated DIR --summary SUMMARY --out PRIVATE_DIR
Public CI:   ... compile --public-fixture --generated PUBLIC_FIXTURE_DIR --out RUNNER_TEMP_DIR

Requires clang-cl and Windows MSVC / Windows SDK development headers for compile.
No game-code uploads, execution, linking, AppX packaging, or network calls.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from private_gamecode import ROOT, inside_repo, inspect_generated

CODE_RE = re.compile(r"code_[0-9]+\.c\Z")
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
COFF_AMD64 = b"\x64\x86"


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def is_under(path, parent):
    p = Path(path).resolve()
    root = Path(parent).resolve()
    return p == root or root in p.parents


def verify_private_summary(generated, summary):
    if summary is None:
        raise ValueError("--summary is required for private game code")
    info = json.loads(Path(summary).expanduser().read_text(encoding="utf-8"))
    if not isinstance(info, dict) or info.get("schema") != "nsmbu-xbox-private-recomp-v1":
        raise ValueError("private summary schema mismatch")
    sys.path.insert(0, str(ROOT / "recomp"))
    import builds
    official = builds.by_name("USA_V0")
    expected = official.sha256.lower()
    claimed = str(info.get("rpx_sha256", "")).lower()
    if info.get("build") != "USA_V0" or not SHA256_RE.fullmatch(claimed) or claimed != expected:
        raise ValueError("summary does not match known USA_V0 RPX SHA256")
    if info.get("build_verified_by_sha256") is not True or info.get("source_data_kept_local") is not True:
        raise ValueError("summary does not assert verified local USA_V0 generation")
    if info.get("native_xbox_linked") is not False or info.get("game_code_executed_on_xbox") is not False:
        raise ValueError("summary must not claim that game code was linked or executed")

    actual = inspect_generated(generated)
    for field in ("entry_point", "function_count", "import_count", "code_files", "code_bytes"):
        if type(actual[field]) is not type(info.get(field)) or actual[field] != info[field]:
            raise ValueError("generated inventory and summary disagree: " + field)
    return info


def verify_fixture(generated):
    meta = inspect_generated(generated)
    if (meta["entry_point"] != "0x02000000" or meta["function_count"] != 1 or
            meta["import_count"] != 0 or meta["code_files"] != 1):
        raise ValueError("public fixture is not the expected synthetic return-42 program")
    return meta


def choose_source(generated, requested):
    root = Path(generated).expanduser().resolve()
    if requested:
        if not CODE_RE.fullmatch(requested):
            raise ValueError("--source must be a basename like code_000.c")
        chosen = root / requested
        if not chosen.is_file() or chosen.is_symlink() or not is_under(chosen, root):
            raise ValueError("requested source missing or outside generated directory")
    else:
        chunks = [p for p in root.glob("code_*.c")
                  if p.is_file() and not p.is_symlink() and CODE_RE.fullmatch(p.name)]
        if not chunks:
            raise ValueError("no generated source chunks")
        # A small chunk is the safest first compiler smoke test; it is
        # NOT assumed to be a leaf function or independently linkable.
        chosen = min(chunks, key=lambda p: (p.stat().st_size, p.name))
    if chosen.stat().st_size == 0:
        raise ValueError("empty generated source file")
    return chosen


def compiler_path(requested):
    compiler = requested or shutil.which("clang-cl")
    if not compiler:
        raise ValueError("clang-cl missing; use Windows VS Developer PowerShell or --compiler")
    path = Path(compiler)
    if path.is_file():
        return str(path.resolve())
    result = shutil.which(compiler)
    if not result:
        raise ValueError("cannot find clang-cl executable")
    return result


def compile_one(generated, selected, outdir, compiler):
    generated = Path(generated).resolve()
    target = Path(outdir).expanduser().resolve()
    if inside_repo(target) or is_under(target, generated) or is_under(generated, target):
        raise ValueError("private output must be separate from generated sources and public checkout")
    if target.exists() and any(target.iterdir()):
        raise ValueError("output directory must be new or empty")
    target.mkdir(parents=True, exist_ok=True)
    obj = target / "gamecode_probe.obj"
    logfile = target / "compile.log"
    command = [
        compiler, "/nologo", "/c", "/O1", "/MD", "/clang:-std=c11",
        "/I" + str(generated), "/I" + str(ROOT / "runtime" / "include"),
        "/Fo" + str(obj), str(selected)
    ]
    # Source text, diagnostics and object bytes stay on the private machine.
    with logfile.open("w", encoding="utf-8", errors="replace") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                check=False)
    if result.returncode:
        raise RuntimeError("clang-cl failed (exit %d); read LOCAL %s" %
                           (result.returncode, logfile))
    if not obj.is_file() or obj.stat().st_size < 20:
        raise RuntimeError("compiler returned success but no COFF object was produced")
    with obj.open("rb") as f:
        machine = f.read(2)
    if machine != COFF_AMD64:
        raise RuntimeError("compiler output is not a Windows x64 AMD64 COFF object")
    return obj


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("plan", "compile"))
    parser.add_argument("--generated", required=True)
    parser.add_argument("--summary")
    parser.add_argument("--public-fixture", action="store_true",
                        help="only for the authored 3-instruction fixture; no game files")
    parser.add_argument("--source", help="single code_NNN.c basename; default: smallest")
    parser.add_argument("--out", help="PRIVATE output folder, outside GitHub checkout")
    parser.add_argument("--compiler", help="clang-cl executable (Windows VS developer environment)")
    args = parser.parse_args(argv)
    try:
        root = Path(args.generated).expanduser().resolve()
        if not root.is_dir() or inside_repo(root):
            raise ValueError("generated sources must exist OUTSIDE the public checkout")
        if args.public_fixture:
            if args.summary:
                raise ValueError("--summary cannot be combined with --public-fixture")
            verify_fixture(root)
        else:
            verify_private_summary(root, args.summary)
        selected = choose_source(root, args.source)
        print("Source verified:", selected.name, "bytes:", selected.stat().st_size)
        print("Source SHA256:", digest(selected))
        print("Note: one .obj is NOT a linked Xbox/UWP executable or playable Mario.")
        if args.action == "plan":
            print("Ready for compile on a PRIVATE Windows x64 machine with clang-cl.")
            return 0
        if not args.out:
            raise ValueError("--out is required for compile")
        compiler = compiler_path(args.compiler)
        obj = compile_one(root, selected, args.out, compiler)
        report = {
            "schema": "nsmbu-xbox-private-coff-probe-v1",
            "build": "PUBLIC_FIXTURE" if args.public_fixture else "USA_V0",
            "source_file": selected.name,
            "source_sha256": digest(selected),
            "coff_machine": "0x8664",
            "object_bytes": obj.stat().st_size,
            "object_sha256": digest(obj),
            "native_xbox_linked": False,
            "game_code_executed_on_xbox": False,
            "private_output_only": True,
        }
        reportfile = obj.parent / "compile-summary.json"
        with reportfile.open("x", encoding="utf-8") as f:
            json.dump(report, f, sort_keys=True, indent=2)
            f.write("\n")
        print("Windows AMD64 COFF object verified; compiler output is PRIVATE.")
        print("Share only compile-summary.json, not .obj, source or compile.log.")
        print("Report:", reportfile)
        return 0
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        parser.exit(1, "Private compilation stopped: %s\n" % exc)


if __name__ == "__main__":
    sys.exit(main())
