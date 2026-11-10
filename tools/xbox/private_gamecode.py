#!/usr/bin/env python3
"""Private local New Super Mario Bros. U recompilation preparation.

prepare --rpx LOCAL/red-pro2.rpx --out ~/NSMBU-Xbox-private/USA_V0
inspect --generated DIR

Does not upload anything or copy game code into a public repository.
Only the user-controlled output outside this Git checkout receives code.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
RECOMP = ROOT / "recomp" / "recomp.py"
PATTERNS = {
    "entry_point": re.compile(r"\bg_recomp_entry_point\s*=\s*0x([0-9A-Fa-f]+)[uU]?\s*;"),
    "function_count": re.compile(r"\bg_recomp_func_count\s*=\s*(\d+)[uU]?\s*;"),
    "import_count": re.compile(r"\bg_recomp_import_count\s*=\s*(\d+)[uU]?\s*;"),
}

def inside_repo(path):
    p = Path(path).resolve()
    return p == ROOT or ROOT in p.parents

def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def inspect_generated(directory):
    root = Path(directory).expanduser().resolve()
    if not root.is_dir():
        raise ValueError("generated code directory not found")
    for name in ("funcs.h", "table.c", "imports.json"):
        if not (root / name).is_file():
            raise ValueError("missing recomp output: " + name)
    chunks = sorted(p for p in root.glob("code_*.c")
                    if p.is_file() and re.fullmatch(r"code_[0-9]+\.c", p.name))
    if not chunks:
        raise ValueError("no translated code_NNN.c files")
    table = (root / "table.c").read_text(encoding="utf-8")
    values = {}
    for key, pattern in PATTERNS.items():
        match = pattern.search(table)
        if not match:
            raise ValueError("missing recomp metadata: " + key)
        values[key] = int(match.group(1), 16 if key == "entry_point" else 10)
    imports = json.loads((root / "imports.json").read_text(encoding="utf-8"))
    if not isinstance(imports, list) or values["import_count"] != len(imports):
        raise ValueError("recomp imports count differs from imports.json")
    if values["entry_point"] == 0 or values["function_count"] < 1:
        raise ValueError("missing entry point or generated function table")
    if not (root / "funcs.h").read_text(encoding="utf-8").startswith("#pragma once"):
        raise ValueError("invalid funcs.h")
    return {
        "schema": "nsmbu-xbox-private-recomp-v1",
        "entry_point": "0x%08X" % values["entry_point"],
        "function_count": values["function_count"],
        "import_count": values["import_count"],
        "code_files": len(chunks),
        "code_bytes": sum(p.stat().st_size for p in chunks),
        "native_xbox_linked": False,
        "game_code_executed_on_xbox": False,
    }

def save_summary(path, info):
    path = Path(path).expanduser().resolve()
    if inside_repo(path):
        raise ValueError("summary destination must be outside public checkout")
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "x", encoding="utf-8") as out:
        json.dump(info, out, indent=2, sort_keys=True)
        out.write("\n")

def prepare(args):
    sys.path.insert(0, str(ROOT / "recomp"))
    import builds
    rpx = Path(args.rpx).expanduser().resolve()
    dest = Path(args.out).expanduser().resolve()
    if not rpx.is_file():
        raise ValueError("local red-pro2.rpx not found")
    if inside_repo(dest):
        raise ValueError("refusing to write game-derived code inside the public GitHub checkout")
    if dest == rpx or dest == rpx.parent or dest in rpx.parents:
        raise ValueError("private output must not contain or overwrite the source RPX")
    if dest.exists() and (not dest.is_dir() or any(dest.iterdir())):
        raise ValueError("output directory is not empty; use a fresh destination")
    if not 1 <= args.jobs <= 16:
        raise ValueError("--jobs must be 1..16")
    sha = file_sha256(rpx)
    build = builds.by_sha256(sha)
    if build is None:
        raise ValueError("unknown RPX SHA256; no code generated")
    if build.name != "USA_V0":
        raise ValueError("expected USA_V0 for this Xbox milestone; got " + build.name)
    dest.mkdir(parents=True, exist_ok=True)
    generated = dest / "generated"
    log = dest / "recompile.log"
    command = [sys.executable, str(RECOMP), str(rpx), str(generated), "--jobs", str(args.jobs)]
    env = dict(os.environ, NSMBU_RECOMP_MOD_HOOKS="0", PYTHONDONTWRITEBYTECODE="1")
    print("USA_V0 SHA-256 confirmed. All game-derived files stay LOCAL.")
    print("Generating local source in:", generated, flush=True)
    with log.open("w", encoding="utf-8") as output:
        status = subprocess.call(command, env=env, stdout=output, stderr=subprocess.STDOUT)
    if status:
        raise RuntimeError("recomp failed (exit %d); inspect LOCAL log %s" % (status, log))
    info = inspect_generated(generated)
    info.update({"build": build.name, "rpx_sha256": sha,
                 "build_verified_by_sha256": True, "source_data_kept_local": True})
    save_summary(dest / "summary.json", info)
    print("Functions:", info["function_count"], "Imports:", info["import_count"],
          "C files:", info["code_files"], "Source bytes:", info["code_bytes"])
    print("Entry:", info["entry_point"])
    print("Share summary.json only; never upload generated/ or recompile.log.")
    print("Summary:", dest / "summary.json")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    gen = commands.add_parser("prepare", help="generate private code from exact USA_V0")
    gen.add_argument("--rpx", required=True)
    gen.add_argument("--out", required=True)
    gen.add_argument("--jobs", type=int, default=4)
    check = commands.add_parser("inspect", help="metadata-only scan of existing generated C")
    check.add_argument("--generated", required=True)
    check.add_argument("--summary", help="optional output file OUTSIDE repository")
    args = parser.parse_args()
    try:
        if args.action == "prepare":
            prepare(args)
        else:
            info = inspect_generated(args.generated)
            info.update({"build": "UNVERIFIED", "build_verified_by_sha256": False})
            if args.summary:
                save_summary(args.summary, info)
            print(json.dumps(info, indent=2, sort_keys=True))
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        parser.exit(1, "Preparation stopped: %s\n" % error)

if __name__ == "__main__":
    main()
