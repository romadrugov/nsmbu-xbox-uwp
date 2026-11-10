#!/usr/bin/env python3
"""Find and privately prepare a minimal actual USA_V0 recompiled function.

This tool NEVER uploads game-derived bytes. It reads local generated
code_NNN.c chunks and writes any extracted C only OUTSIDE the repository.

Commands:
  scan      --generated PRIVATE/USA_V0/generated --out PRIVATE/r24
  extract   --generated PRIVATE/USA_V0/generated --out PRIVATE/r24 --address 0x...
  reference --out PRIVATE/r24

The first candidate class is deliberately strict: straight-line integer
register arithmetic, unconditional PPC blr, and no guest memory, hooks,
imports, branches or calls. This is a smoke test, not game initialization.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

import private_gamecode

ROOT = Path(__file__).resolve().parents[2]
FUNCTION = re.compile(r"(?m)^void f_([0-9A-F]{8})\(Cpu\* __restrict c\) \{$")
FINISH = re.compile(r"(?m)^}\s*$")
INSN = re.compile(r"/\*\s*([0-9A-F]{8}):\s*([0-9A-F]{8})\s*\*/")
RETURN_BLR = re.compile(r"\breturn\s*;\s*/\*\s*[0-9A-F]{8}:\s*4E800020\s*\*/")
FALLTHROUGH = re.compile(
    r"\s*(?:MUSTTAIL return f_[0-9A-F]{8}(?:_orig)?\(c\);"
    r"(?:\s*/\*[^\n]*\*/)?\s*)?\Z"
)
C_CALL = re.compile(r"(?<![\w])([A-Za-z_]\w*)\s*\(")
DANGEROUS = re.compile(
    r"\b(?:if|for|while|switch|goto|case|default|break|continue|"
    r"ld8|ld16|ld32|ld64|st8|st16|st32|st64|"
    r"ppc_ptr|ppc_dispatch|ppc_unimplemented|ppc_trap|"
    r"ppc_lwarx|ppc_stwcx|ppc_dcbz|psq_load|psq_store|"
    r"memcpy|memset|memmove|__atomic|MUSTTAIL|"
    r"hook_[0-9A-Fa-f]+|site_[0-9A-Fa-f]+|imp_\w+|"
    r"f_[0-9A-F]{8})\b"
)
DIRECT_WRITE = re.compile(r"\bc->r\[3\]\s*(?:[+\-*/%&|^]?=(?!=)|\+\+|--)")
BRANCH_LABEL = re.compile(r"\bL_[0-9A-F]{8}\s*:")
SINGLE_LINE_COMMENT = re.compile(r"//[^\n]*")
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)


def chunks(generated):
    root = Path(generated).expanduser().resolve()
    if not root.is_dir():
        raise ValueError("generated directory missing")
    files = sorted(root.glob("code_*.c"))
    files = [p for p in files if re.fullmatch(r"code_\d+\.c", p.name)]
    if not files:
        raise ValueError("generated directory has no code_NNN.c chunks")
    return root, files


def candidates_in_file(path):
    source = path.read_text(encoding="utf-8")
    for match in FUNCTION.finditer(source):
        finish = FINISH.search(source, match.end())
        if finish is None:
            continue
        inner = source[match.end():finish.start()]
        marks = list(INSN.finditer(inner))
        if not (2 <= len(marks) <= 24):
            continue
        if marks[-1].group(2) != "4E800020":
            continue
        last = RETURN_BLR.search(inner)
        if last is None or last.end() != marks[-1].end():
            continue
        # The recompiler may append an unreachable call to the following
        # function. Drop it ONLY after the verified unconditional blr.
        suffix = inner[last.end():]
        if not FALLTHROUGH.fullmatch(suffix):
            continue
        body = inner[:last.end()]
        without_comments = BLOCK_COMMENT.sub("", SINGLE_LINE_COMMENT.sub("", body))
        if DANGEROUS.search(without_comments) or BRANCH_LABEL.search(without_comments):
            continue
        if any(call != "PPC_ENTER" for call in C_CALL.findall(without_comments)):
            continue
        if not DIRECT_WRITE.search(without_comments):
            continue
        # No references to unspecified thread, memory or host platform state.
        if re.search(r"\bc->(?:thread|res_addr|res_val)\b", without_comments):
            continue
        # A candidate's source code is private and is never in the JSON.
        yield {
            "address": "0x" + match.group(1),
            "file": path.name,
            "instructions": len(marks),
            "last_ppc_opcode": "4E800020",
            "risk": "straight-line-register-only",
            "_body": "void nsmbu_r24_private_function(Cpu* __restrict c) {"
                     + body.rstrip() + "\n}\n",
        }


def scan(generated, limit=10):
    root, files = chunks(generated)
    found = []
    for path in files:
        for item in candidates_in_file(path):
            found.append(item)
    found.sort(key=lambda x: (x["instructions"], x["address"]))
    return found[:limit], len(found), len(files)


def address_value(raw):
    value = int(raw, 0)
    if not (0x02000000 <= value < 0x03000000) or value & 3:
        raise ValueError("candidate must be an aligned USA_V0 PPC text address")
    return value


def private_out(path):
    path = Path(path).expanduser().resolve()
    if private_gamecode.inside_repo(path):
        raise ValueError("refusing to write derived code inside public checkout")
    if path == Path.home():
        raise ValueError("output must be a dedicated private directory")
    path.mkdir(parents=True, exist_ok=True)
    return path


def scan_command(args):
    entries, total, file_count = scan(args.generated, args.limit)
    out = private_out(args.out)
    public_metadata = [{k: v for k, v in entry.items() if not k.startswith("_")}
                       for entry in entries]
    report = {
        "schema": "nsmbu-xbox-r24-leaf-candidates-v1",
        "number_of_scanned_chunks": file_count,
        "strict_candidates": total,
        "shortlist": public_metadata,
        "real_game_code_executed_on_xbox": False,
    }
    (out / "candidates.json").write_text(json.dumps(report, indent=2) + "\n")
    print("R2.4 local scan: %d strict leaf candidates across %d chunks" % (total, file_count))
    for item in public_metadata:
        print("%s  %d PPC instructions  %s" %
              (item["address"], item["instructions"], item["file"]))
    print("Metadata: " + str(out / "candidates.json"))
    if not entries:
        raise ValueError("no strict leaf found; do NOT execute an unverified fallback")


def extract_command(args):
    target = "0x%08X" % address_value(args.address)
    root, files = chunks(args.generated)
    out = private_out(args.out)
    if out == root or root in out.parents:
        raise ValueError("use a dedicated private output, not the generated code directory")
    for file in files:
        for candidate in candidates_in_file(file):
            if candidate["address"] != target:
                continue
            code = "#include \"ppc.h\"\n\n" + candidate["_body"]
            (out / "r24_selected.c").write_text(code, encoding="utf-8")
            (out / "r24_reference.c").write_text(reference_source(target), encoding="utf-8")
            info = {k: v for k, v in candidate.items() if not k.startswith("_")}
            (out / "selected.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
            print("Extracted strictly independent game function", target)
            print("All game-derived C remains PRIVATE in", out)
            return
    raise ValueError("address did not pass strict leaf checks; refuse to extract")


def reference_source(address):
    # This harness is PRIVATE because even function addresses are tied to
    # the user's private RPX. None of its output is sent to GitHub CI.
    return """
#include "ppc.h"
#include <stdio.h>
#include <stdlib.h>
int g_ppc_trace = 0;
volatile int g_core_preempt[3] = {0, 0, 0};
void ppc_trace_enter(uint32_t addr) { (void)addr; abort(); }
void ppc_preempt(Cpu* cpu) { (void)cpu; abort(); }
void nsmbu_r24_private_function(Cpu* c);
int main(void) {
    Cpu cpu = {0};
    cpu.pc = %su;
    cpu.core = 0;
    cpu.r[3] = 0x13579BDFu;
    cpu.r[4] = 0x2468ACE0u;
    cpu.r[5] = 0x10203040u;
    cpu.r[6] = 7u;
    cpu.r[7] = 3u;
    nsmbu_r24_private_function(&cpu);
    printf("R3=%%08X R4=%%08X LR=%%08X\\n", cpu.r[3], cpu.r[4], cpu.lr);
    return 0;
}
""" % address


def reference_command(args):
    out = Path(args.out).expanduser().resolve()
    if private_gamecode.inside_repo(out):
        raise ValueError("refusing to use derived code inside public checkout")
    selected = json.loads((out / "selected.json").read_text(encoding="utf-8"))
    addr = "0x%08X" % address_value(selected["address"])
    if not (out / "r24_selected.c").is_file():
        raise ValueError("first run extract to produce private source")
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise ValueError("C compiler not found: " + args.compiler)
    executable = out / "r24_reference_host"
    command = [
        compiler, "-std=c11", "-O2",
        "-I", str(ROOT / "runtime" / "include"),
        str(out / "r24_selected.c"), str(out / "r24_reference.c"),
        "-o", str(executable),
    ]
    subprocess.run(command, check=True, cwd=out)
    result = subprocess.run([str(executable)], cwd=out, check=True,
                            capture_output=True, text=True, timeout=10)
    match = re.fullmatch(r"R3=([0-9A-F]{8}) R4=([0-9A-F]{8}) LR=([0-9A-F]{8})\s*", result.stdout)
    if not match:
        raise ValueError("unexpected local host CPU signature; refuse to write binding")
    r3, r4, lr = (int(item, 16) for item in match.groups())
    # This private binding can later be compiled as Windows Store x64 with
    # clang-cl; symbols have C linkage and no global generated table overlap.
    binding = (
        '#include "ppc.h"\n'
        'void nsmbu_r24_private_function(Cpu* c);\n'
        'PpcFunc g_r24_private_function = nsmbu_r24_private_function;\n'
        'const uint32_t g_r24_guest_address = %su;\n'
        'const uint32_t g_r24_expected_r3 = 0x%08Xu;\n'
        'const uint32_t g_r24_expected_r4 = 0x%08Xu;\n'
        'const uint32_t g_r24_expected_lr = 0x%08Xu;\n'
    ) % (addr, r3, r4, lr)
    (out / "r24_private_binding.c").write_text(binding, encoding="utf-8")
    witness = {"schema": "nsmbu-xbox-r24-local-reference-v1",
               "address": addr, "r3": "0x%08X" % r3,
               "r4": "0x%08X" % r4, "lr": "0x%08X" % lr,
               "reference_executed_locally": True,
               "game_code_executed_on_xbox": False}
    (out / "r24_reference_result.json").write_text(
        json.dumps(witness, indent=2) + "\n", encoding="utf-8")
    print("Local CPU witness: R3=%08X R4=%08X LR=%08X" % (r3, r4, lr))
    print("PRIVATE binding ready:", out / "r24_private_binding.c")
    print("Only local reference executed; NO Xbox code linked or executed yet.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command", required=True)
    sc = subs.add_parser("scan")
    sc.add_argument("--generated", required=True)
    sc.add_argument("--out", required=True)
    sc.add_argument("--limit", type=int, default=10)
    ex = subs.add_parser("extract")
    ex.add_argument("--generated", required=True)
    ex.add_argument("--out", required=True)
    ex.add_argument("--address", required=True)
    ref = subs.add_parser("reference")
    ref.add_argument("--out", required=True)
    ref.add_argument("--compiler", default="clang")
    args = parser.parse_args(argv)
    try:
        if args.command == "scan":
            if not (1 <= args.limit <= 100):
                raise ValueError("--limit must be 1..100")
            scan_command(args)
        elif args.command == "extract":
            extract_command(args)
        else:
            reference_command(args)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired, json.JSONDecodeError) as error:
        parser.exit(1, "R2.4 stopped: %s\n" % error)

if __name__ == "__main__":
    main()
