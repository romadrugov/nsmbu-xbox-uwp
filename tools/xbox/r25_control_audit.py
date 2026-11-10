#!/usr/bin/env python3
"""R2.5 read-only local branch-edge audit for one private recomp function.

This is a structural audit, NOT a proof that a function is safe to
execute. No game-derived C, opcodes or private files leave the Mac.
Output contains branch *addresses* and counts only.
"""
import argparse
import json
from pathlib import Path
import re

import private_gamecode
from r24_leaf_select import FUNCTION, FINISH, INSN
from r25_branch_scout import evaluate

LOCAL_GOTO = re.compile(r"\bgoto\s+L_([0-9A-F]{8})\s*;")
LABEL = re.compile(r"(?m)^\s*L_([0-9A-F]{8})\s*:")
ANNOTATED_LINE = re.compile(r"/\*\s*([0-9A-F]{8}):\s*([0-9A-F]{8})\s*\*/")
TAIL = re.compile(
    r"(?:\s*MUSTTAIL return f_[0-9A-F]{8}(?:_orig)?\(c\);"
    r"(?:\s*/\*[^\n]*\*/)?\s*|"
    r"\s*ppc_unimplemented\(c,\s*0x[0-9A-F]{8}u,\s*0\);"
    r"\s*/\* fell off end of text \*/\s*)\Z"
)
BLR = 0x4E800020


def branch_target(addr, opcode):
    major = opcode >> 26
    if major == 16:  # bc/bcl
        offset = opcode & 0xFFFC
        if offset & 0x8000:
            offset -= 0x10000
    elif major == 18:  # b/bl
        offset = opcode & 0x03FFFFFC
        if offset & 0x02000000:
            offset -= 0x04000000
    else:
        raise ValueError("goto is not annotated by a PPC branch opcode")
    return ((0 if opcode & 2 else addr) + offset) & 0xFFFFFFFF


def read_function(generated, address):
    generated = Path(generated).expanduser().resolve()
    if not generated.is_dir():
        raise ValueError("private generated directory does not exist")
    if not re.fullmatch(r"0x[0-9a-fA-F]{8}", address):
        raise ValueError("give a canonical 8-digit 0x PPC address")
    func_address = address[2:].upper()
    matches = []
    for path in sorted(generated.glob("code_*.c")):
        if not re.fullmatch(r"code_\d+\.c", path.name):
            continue
        text = path.read_text(encoding="utf-8")
        for match in FUNCTION.finditer(text):
            if match.group(1) != func_address:
                continue
            end = FINISH.search(text, match.end())
            if end is None:
                raise ValueError("selected function body cannot be delimited")
            matches.append((path.name, text[match.end():end.start()]))
    if len(matches) != 1:
        raise ValueError("selected address was not found exactly once")
    return matches[0]


def audit(source_name, addr, inner):
    candidate, reason = evaluate(addr[2:].upper(), source_name, inner, 4, 256)
    if candidate is None:
        return {
            "address": addr, "file": source_name,
            "status": "REVIEW_REJECTED",
            "reason": reason, "safe_to_execute": False,
            "code_executed": False,
        }

    original = list(INSN.finditer(inner))
    annotated = {int(m.group(1), 16): int(m.group(2), 16) for m in original}
    labels = {int(n, 16) for n in LABEL.findall(inner)}
    goto_targets = []
    failures = []
    for line in inner.splitlines():
        targets = LOCAL_GOTO.findall(line)
        if not targets:
            continue
        annotation = ANNOTATED_LINE.findall(line)
        if len(targets) != 1 or len(annotation) != 1:
            failures.append("ambiguous_goto_annotation")
            continue
        pos, raw = annotation[0]
        pc, word = int(pos, 16), int(raw, 16)
        dst = int(targets[0], 16)
        if pc not in annotated or annotated[pc] != word:
            failures.append("unknown_branch_source")
            continue
        try:
            decoded = branch_target(pc, word)
        except ValueError:
            failures.append("goto_not_PPC_branch")
            continue
        if dst != decoded:
            failures.append("PPC_branch_target_mismatch")
        if dst not in labels:
            failures.append("goto_target_has_no_local_label")
        if dst <= pc:
            failures.append("backward_or_self_edge")
        goto_targets.append({
            "from": f"0x{pc:08X}",
            "to": f"0x{dst:08X}",
            "target_matches_ppc": decoded == dst,
        })

    number_conditional = sum(
        1 for m in original
        if int(m.group(2), 16) >> 26 == 16 and int(m.group(2), 16) & 1 == 0
    )
    number_blr = sum(int(m.group(2), 16) == BLR for m in original)
    if len(goto_targets) != candidate["local_gotos"]:
        failures.append("not_all_gotos_accounted_for")
    if number_blr == 0:
        failures.append("no_explicit_blr")
    # Important distinction: structural matching is not reachability
    # proof. Missing/extra return paths and uninitialized local state
    # require a human review and multiple bounded reference vectors.
    return {
        "address": addr, "file": source_name,
        "instructions": len(original),
        "conditional_ppc_branches": number_conditional,
        "blr_instructions": number_blr,
        "local_labels": len(labels),
        "branch_edges": goto_targets,
        "generated_fallthrough_detected": bool(TAIL.search(inner)),
        "structural_errors": sorted(set(failures)),
        "status": "STRUCTURAL_AUDIT_PASS_REVIEW_REQUIRED"
                  if not failures else "STRUCTURAL_AUDIT_FAIL",
        "safe_to_execute": False,
        "code_executed": False,
        "private_source_extracted": False,
        "next": "Manually review reachability and build branch-coverage reference vectors.",
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--generated", required=True)
    p.add_argument("--address", default="0x02AFC380")
    p.add_argument("--out", required=True)
    args = p.parse_args()
    try:
        out = Path(args.out).expanduser().resolve()
        if private_gamecode.inside_repo(out):
            raise ValueError("refusing to save private audit under public Git checkout")
        source_name, inner = read_function(args.generated, args.address)
        result = audit(source_name, args.address.upper().replace("0X", "0x"), inner)
        out.mkdir(parents=True, exist_ok=True)
        dest = out / "r25_control_audit.json"
        dest.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
        print(f"Metadata-only audit saved: {dest}")
        print("No source exported, no code executed, NO device validation.")
    except (OSError, ValueError) as error:
        p.exit(1, f"R2.5 audit stopped: {error}\n")


if __name__ == "__main__":
    main()
