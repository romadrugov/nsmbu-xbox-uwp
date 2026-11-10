#!/usr/bin/env python3
"""Game-free regression tests for R2.4 private leaf function selector."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile
import unittest

import r24_leaf_select as leaf


GOOD = """#include "funcs.h"

void f_02001234(Cpu* __restrict c) {
    PPC_ENTER(0x02001234u);
    c->r[3] = c->r[4] + 3u; /* 02001234: 38640003 */
    return; /* 02001238: 4E800020 */
    MUSTTAIL return f_0200123C(c);
}
"""
BAD_MEMORY = """#include "funcs.h"
void f_02002000(Cpu* __restrict c) {
    PPC_ENTER(0x02002000u);
    c->r[3] = ld32(c->r[4]); /* 02002000: 80640000 */
    return; /* 02002004: 4E800020 */
}
"""
BAD_CALL = """#include "funcs.h"
void f_02002200(Cpu* __restrict c) {
    PPC_ENTER(0x02002200u);
    c->r[3] = 7u; /* 02002200: 38600007 */
    if (c->r[4] == 5) return; /* 02002204: 4E800020 */
}
"""
BAD_UNHANDLED = """#include "funcs.h"
void f_02003000(Cpu* __restrict c) {
    PPC_ENTER(0x02003000u);
    ppc_unimplemented(c, 0x02003000u, 1); /* 02003000: 00000001 */
    return; /* 02003004: 4E800020 */
}
"""


class R24SelectorTest(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.gen = self.root / "generated"
        self.gen.mkdir()
        self.private = self.root / "r24"
        (self.gen / "code_000.c").write_text(
            GOOD + "\n" + BAD_MEMORY + "\n" + BAD_CALL + "\n" + BAD_UNHANDLED,
            encoding="utf-8")

    def test_excludes_memory_calls_and_branches(self):
        candidates = list(leaf.candidates_in_file(self.gen / "code_000.c"))
        self.assertEqual([x["address"] for x in candidates], ["0x02001234"])
        self.assertNotIn("MUSTTAIL return f_", candidates[0]["_body"])
        self.assertIn("c->r[3] = c->r[4] + 3u;", candidates[0]["_body"])

    def test_scan_metadata_never_contains_game_code(self):
        args = argparse.Namespace(generated=str(self.gen), out=str(self.private), limit=5)
        leaf.scan_command(args)
        output = json.loads((self.private / "candidates.json").read_text())
        self.assertEqual(output["strict_candidates"], 1)
        self.assertEqual(output["shortlist"][0]["address"], "0x02001234")
        self.assertNotIn("c->r[3]", json.dumps(output))
        self.assertFalse(output["real_game_code_executed_on_xbox"])

    def test_extract_and_local_reference(self):
        args = argparse.Namespace(generated=str(self.gen), out=str(self.private),
                                  address="0x02001234")
        leaf.extract_command(args)
        code = (self.private / "r24_selected.c").read_text()
        self.assertNotIn("f_0200123C", code)
        self.assertIn("void nsmbu_r24_private_function", code)
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("no host C compiler available")
        leaf.reference_command(argparse.Namespace(out=str(self.private), compiler=compiler))
        witness = json.loads((self.private / "r24_reference_result.json").read_text())
        self.assertEqual(witness["r3"], "0x2468ACE3")
        self.assertEqual(witness["r4"], "0x2468ACE0")
        self.assertEqual(witness["lr"], "0x00000000")
        binding = (self.private / "r24_private_binding.c").read_text()
        self.assertIn("g_r24_expected_r3 = 0x2468ACE3u", binding)

    def test_no_repo_writes(self):
        with self.assertRaisesRegex(ValueError, "public checkout"):
            leaf.private_out(leaf.ROOT / "game" / "r24")
        with self.assertRaises(ValueError):
            leaf.address_value("0x10010000")
        with self.assertRaises(ValueError):
            leaf.address_value("0x02000001")
        with self.assertRaisesRegex(ValueError, "strict leaf"):
            leaf.extract_command(argparse.Namespace(
                generated=str(self.gen), out=str(self.private), address="0x02002000"))


class R24DiagnosticsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.gen = self.root / "gen"
        self.gen.mkdir()
        self.output = self.root / "r24"

    def test_early_unconditional_blr_discard_only_unreachable_suffix(self):
        # The actual recompiler emits instructions following unconditional
        # blr when a discovered function covers more bytes than its first
        # return. Only a completely straight-line prefix may be selected.
        body = """void f_02005600(Cpu* __restrict c) {
    PPC_ENTER(0x02005600u);
    uint32_t r3 = c->r[3];
    r3 = c->r[4] + 9u; /* 02005600: 38640009 */
    { c->r[3] = r3; return; } /* 02005604: 4E800020 */
    c->r[4] = ld32(c->r[6]); /* 02005608: 80860000 */
    ppc_unimplemented(c, 0x0200560Cu, 0); /* fell off end of text */
}
"""
        (self.gen / "code_000.c").write_text(body)
        candidates, total, _, reasons = leaf.scan(
            self.gen, with_diagnostics=True)
        self.assertEqual(total, 1)
        self.assertEqual(reasons["eligible"], 1)
        self.assertEqual(candidates[0]["instructions"], 2)
        self.assertEqual(candidates[0]["original_instructions"], 3)
        self.assertEqual(candidates[0]["ignored_after_unconditional_return"], 1)
        self.assertNotIn("ld32", candidates[0]["_body"])
        self.assertNotIn("ppc_unimplemented", candidates[0]["_body"])

    def test_branch_before_return_is_never_truncated(self):
        body = """void f_02006000(Cpu* __restrict c) {
    PPC_ENTER(0x02006000u);
    if (c->r[4]) goto L_0200600C; /* 02006000: 4082000C */
    c->r[3] = 8; /* 02006004: 38600008 */
    return; /* 02006008: 4E800020 */
L_0200600C:
    c->r[3] = ld32(c->r[4]); /* 0200600C: 80640000 */
}
"""
        (self.gen / "code_000.c").write_text(body)
        c, total, n, reasons = leaf.scan(self.gen, with_diagnostics=True)
        self.assertEqual((total, n), (0, 1))
        self.assertEqual(reasons["branch_or_complex_control_flow"], 1)

    def test_funnel_reports_without_exposing_source_or_executing(self):
        (self.gen / "code_000.c").write_text(
            GOOD + "\n" + BAD_MEMORY + "\n" + BAD_CALL)
        leaf.scan_command(argparse.Namespace(
            generated=str(self.gen), out=str(self.output), limit=5))
        report = json.loads((self.output / "candidates.json").read_text())
        self.assertEqual(report["schema"], "nsmbu-xbox-r24-leaf-candidates-v2")
        self.assertEqual(report["functions_examined"], 3)
        self.assertEqual(sum(report["reasons"].values()), 3)
        self.assertEqual(report["strict_candidates"], 1)
        self.assertEqual(report["reasons"]["memory_import_or_host_side_effect"], 1)
        self.assertEqual(report["reasons"]["branch_or_complex_control_flow"], 1)
        self.assertNotIn("ld32(", json.dumps(report))
        self.assertNotIn("c->r[3]", json.dumps(report))
        self.assertFalse(report["real_game_code_executed_on_xbox"])

    def test_zero_candidates_is_diagnostic_not_arbitrary_fallback(self):
        (self.gen / "code_000.c").write_text(BAD_MEMORY)
        leaf.scan_command(argparse.Namespace(
            generated=str(self.gen), out=str(self.output), limit=5))
        report = json.loads((self.output / "candidates.json").read_text())
        self.assertEqual(report["strict_candidates"], 0)
        self.assertEqual(report["functions_examined"], 1)
        self.assertEqual(report["shortlist"], [])
        self.assertFalse((self.output / "r24_selected.c").exists())

    def test_unknown_host_helper_is_rejected(self):
        body = """void f_02007000(Cpu* __restrict c) {
    PPC_ENTER(0x02007000u);
    c->r[3] = unsafe_custom_helper(c->r[4]); /* 02007000: 38640001 */
    return; /* 02007004: 4E800020 */
}
"""
        (self.gen / "code_000.c").write_text(body)
        _, count, _, stats = leaf.scan(self.gen, with_diagnostics=True)
        self.assertEqual(count, 0)
        self.assertEqual(stats["unknown_helper_call"], 1)

    def test_no_unconditional_return_reports_reason(self):
        body = """void f_02008000(Cpu* __restrict c) {
    PPC_ENTER(0x02008000u);
    c->r[3] = c->r[4] + 1; /* 02008000: 38640001 */
    MUSTTAIL return f_02008004(c); /* 02008004: 48000000 */
}
"""
        (self.gen / "code_000.c").write_text(body)
        _, count, _, stats = leaf.scan(self.gen, with_diagnostics=True)
        self.assertEqual(count, 0)
        self.assertEqual(stats["no_unconditional_blr"], 1)


if __name__ == "__main__":
    unittest.main()
