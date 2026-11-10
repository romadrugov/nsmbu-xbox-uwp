#!/usr/bin/env python3
"""R2.5 *read-only* scout for more substantial PowerPC control-flow functions.

This does NOT extract, compile, execute, or upload game-derived source.
It identifies plausible register-only, locally branching functions from
PRIVATE generated code_*.c files for subsequent manual control-flow review.

The R2.4 extraction/execution path must NOT be used for these candidates.
A branch may reach a generated fallthrough, a missing label, or code outside
the inspected region even when this heuristic reports REVIEW ONLY.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

import private_gamecode
from r24_leaf_select import FUNCTION, FINISH, INSN, MEMORY_OR_IMPORT
from r24_leaf_select import SINGLE_LINE_COMMENT, BLOCK_COMMENT, PURE_CALLS

CONDITIONAL = re.compile(r"\bif\s*\(")
BRANCH_GOTO = re.compile(r"\bgoto\s+(L_[0-9A-F]{8})\s*;")
LOCAL_LABEL = re.compile(r"(?m)^\s*(L_[0-9A-F]{8})\s*:")
C_CALL = re.compile(r"(?<![\w])([A-Za-z_]\w*)\s*\(")
RESULT = re.compile(r"\bc->r\[[34]\]\s*=(?!=)")
RETURN_BLR = 0x4E800020
REJECT_CONTROL = re.compile(
    r"\b(?:for|while|do|switch|case|default|PPC_LOOP|setjmp|longjmp|"
    r"break|continue|__asm__|asm|volatile)\b"
)
REASON_NAMES = (
    "missing_instructions",
    "outside_size_range",
    "no_conditional_ppc_branch",
    "no_blr",
    "memory_or_runtime_dependency",
    "indirect_or_loop_control",
    "unknown_helper_call",
    "external_or_backward_goto",
    "no_register_result",
    "review_only",
)


def evaluate(addr, filename, source, min_instructions, max_instructions):
    marks = list(INSN.finditer(source))
    if not marks:
        return None, "missing_instructions"
    if not min_instructions <= len(marks) <= max_instructions:
        return None, "outside_size_range"
    # PowerPC major opcode 16 = conditional bc/bcl. Ignore link branches.
    conditional = [(m.group(1), int(m.group(2), 16))
                   for m in marks
                   if int(m.group(2), 16) >> 26 == 16
                   and int(m.group(2), 16) & 1 == 0]
    if not conditional:
        return None, "no_conditional_ppc_branch"
    if not any(int(m.group(2), 16) == RETURN_BLR for m in marks):
        return None, "no_blr"

    # Skip just the generated *end-of-function* synthetic fallthrough,
    # never strip code before any branch or BLR (no reachability claim).
    end_tail = re.search(
        r"(?:\s*MUSTTAIL return f_[0-9A-F]{8}(?:_orig)?\(c\);"
        r"(?:\s*/\*[^\n]*\*/)?\s*|"
        r"\s*ppc_unimplemented\(c,\s*0x[0-9A-F]{8}u,\s*0\);"
        r"\s*/\* fell off end of text \*/\s*)\Z",
        source)
    inspected = source[:end_tail.start()] if end_tail else source
    code = BLOCK_COMMENT.sub("", SINGLE_LINE_COMMENT.sub("", inspected))
    if MEMORY_OR_IMPORT.search(code):
        return None, "memory_or_runtime_dependency"
    if REJECT_CONTROL.search(code) or re.search(
        r"\b(?:ppc_dispatch|ppc_unimplemented|ppc_trap)\s*\(", code):
        return None, "indirect_or_loop_control"
    allowed = set(PURE_CALLS) | {"if"}
    if any(name not in allowed for name in C_CALL.findall(code)):
        return None, "unknown_helper_call"

    labels = {m.group(1): int(m.group(1)[2:], 16)
              for m in LOCAL_LABEL.finditer(code)}
    gotos = [m.group(1) for m in BRANCH_GOTO.finditer(code)]
    # The conditional branch must be expressed in the generated C;
    # labels and target validations are deliberately conservative.
    if not CONDITIONAL.search(code) or not gotos:
        return None, "external_or_backward_goto"
    if any(label not in labels for label in gotos):
        return None, "external_or_backward_goto"
    # A backward target can loop; stage R2.5 first validates acyclic
    # local branches only. No automatic execution is ever permitted.
    branch_sources = [int(m.group(1), 16) for m in marks
                      if int(m.group(2), 16) >> 26 in (16, 18)]
    if branch_sources and any(labels[label] <= min(branch_sources)
                              for label in gotos):
        return None, "external_or_backward_goto"
    if not RESULT.search(code):
        return None, "no_register_result"

    candidate = {
        "address": "0x" + addr,
        "file": filename,
        "instructions": len(marks),
        "conditional_ppc_branches": len(conditional),
        "local_labels": len(labels),
        "local_gotos": len(gotos),
        "generated_fallthrough": bool(end_tail),
        "status": "REVIEW_ONLY_DO_NOT_EXECUTE",
        "privacy": "metadata_only",
    }
    return candidate, "review_only"


def scout(generated, out, limit=15, min_instructions=8, max_instructions=96):
    source_dir = Path(generated).expanduser().resolve()
    out_dir = Path(out).expanduser().resolve()
    if not source_dir.is_dir():
        raise ValueError("private generated source directory missing")
    if private_gamecode.inside_repo(out_dir):
        raise ValueError("refusing to write report in public checkout")
    if out_dir == source_dir or source_dir in out_dir.parents:
        raise ValueError("reports must not overwrite generated game sources")
    chunks = sorted(p for p in source_dir.glob("code_*.c")
                    if re.fullmatch(r"code_\d+\.c", p.name))
    if not chunks:
        raise ValueError("no code_NNN.c files found")

    candidates = []
    reasons = Counter()
    for path in chunks:
        source = path.read_text(encoding="utf-8")
        for match in FUNCTION.finditer(source):
            close = FINISH.search(source, match.end())
            if close is None:
                reasons["missing_instructions"] += 1
                continue
            item, reason = evaluate(match.group(1), path.name,
                                    source[match.end():close.start()],
                                    min_instructions, max_instructions)
            reasons[reason] += 1
            if item:
                candidates.append(item)
    candidates.sort(key=lambda x: (
        abs(x["instructions"] - 24),
        x["conditional_ppc_branches"],
        x["address"],
    ))
    report = {
        "schema": "nsmbu-r25-branch-scout-v1",
        "generated_chunks_scanned": len(chunks),
        "functions_examined": sum(reasons.values()),
        "review_candidates_total": len(candidates),
        "reasons": {name: reasons[name] for name in REASON_NAMES},
        "shortlist": candidates[:limit],
        "private_source_extracted": False,
        "code_executed": False,
        "xbox_device_test_pass": False,
        "warning": "REVIEW ONLY. No candidate is verified as safely executable.",
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "r25_branch_scout.json"
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"R2.5 scout: {len(chunks)} chunks, {report['functions_examined']} functions")
    print(f"Branch candidates FOR REVIEW ONLY: {len(candidates)}")
    for name in REASON_NAMES:
        print(f"  {name:37s} {reasons[name]:7d}")
    for item in candidates[:limit]:
        print(f"{item['address']}   {item['instructions']} insns "
              f"{item['conditional_ppc_branches']} conditional branch(es) "
              f"{item['local_gotos']} goto(s) {item['file']}")
    print(f"Metadata-only report: {path}")
    print("No function extracted, compiled or executed. Next: manually audit control flow.")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--limit", type=int, default=15)
    parser.add_argument("--min-instructions", type=int, default=8)
    parser.add_argument("--max-instructions", type=int, default=96)
    args = parser.parse_args()
    if not (1 <= args.limit <= 100 and
            4 <= args.min_instructions <= args.max_instructions <= 256):
        parser.error("limits must be 1..100 and instruction range 4..256")
    try:
        scout(args.generated, args.out, args.limit,
              args.min_instructions, args.max_instructions)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"R2.5 scout stopped: {exc}\n")


if __name__ == "__main__":
    main()
