"""Tests for R2.5 exact forward PPC edge decoding: synthetic code only."""
import json
from pathlib import Path
import tempfile
import unittest

from r25_control_audit import audit, branch_target, read_function


GOOD = """
void f_02001000(Cpu* __restrict c) {
    PPC_ENTER(0x02001000u);
    uint32_t r3 = c->r[3]; /* 02001000: 38630000 */
    if (c->r[4]) { goto L_02001018; } /* 02001004: 40820014 */
    r3 += 1; /* 02001008: 38630001 */
    r3 += 2; /* 0200100C: 38630002 */
    goto L_0200101C; /* 02001010: 4800000C */
    r3 += 4; /* 02001014: 38630004 */
L_02001018: ;
    r3 += 8; /* 02001018: 38630008 */
L_0200101C: ;
    c->r[3] = r3; /* 0200101C: 38630000 */
    return; /* 02001020: 4E800020 */
    MUSTTAIL return f_02001024(c);
}
"""


def body(src):
    return src.split("{", 1)[1].rsplit("}", 1)[0]


class ControlAuditTests(unittest.TestCase):
    def test_decode_actual_ppc_relative_targets(self):
        self.assertEqual(branch_target(0x02001004, 0x40820014), 0x02001018)
        self.assertEqual(branch_target(0x02001010, 0x4800000C), 0x0200101C)
        self.assertEqual(branch_target(0x02001020, 0x4BFFFFF8), 0x02001018)
        self.assertEqual(branch_target(0x02001020, 0x4082FFF8), 0x02001018)
        with self.assertRaises(ValueError):
            branch_target(0x02001020, 0x38600000)

    def test_structural_edges_pass_but_not_auto_executable(self):
        result = audit("code_000.c", "0x02001000", body(GOOD))
        self.assertEqual(result["status"], "STRUCTURAL_AUDIT_PASS_REVIEW_REQUIRED")
        self.assertFalse(result["safe_to_execute"])
        self.assertFalse(result["code_executed"])
        self.assertFalse(result["private_source_extracted"])
        self.assertEqual(result["conditional_ppc_branches"], 1)
        self.assertEqual(result["blr_instructions"], 1)
        self.assertEqual(len(result["branch_edges"]), 2)
        self.assertTrue(all(e["target_matches_ppc"] for e in result["branch_edges"]))
        self.assertTrue(result["generated_fallthrough_detected"])
        dumped = json.dumps(result)
        self.assertNotIn("r3 += 1", dumped)
        self.assertNotIn("MUSTTAIL return", dumped)

    def test_branch_target_mismatch_is_not_pass(self):
        bad = GOOD.replace("40820014", "40820010")
        result = audit("code_000.c", "0x02001000", body(bad))
        self.assertEqual(result["status"], "STRUCTURAL_AUDIT_FAIL")
        self.assertIn("PPC_branch_target_mismatch", result["structural_errors"])

    def test_wrong_code_opcode_is_not_pass(self):
        bad = GOOD.replace("4800000C", "38600000")
        result = audit("code_000.c", "0x02001000", body(bad))
        self.assertEqual(result["status"], "STRUCTURAL_AUDIT_FAIL")
        self.assertIn("goto_not_PPC_branch", result["structural_errors"])

    def test_private_lookup_finds_exact_selected_body(self):
        with tempfile.TemporaryDirectory() as temp:
            generated = Path(temp)
            (generated / "code_092.c").write_text(GOOD, encoding="utf-8")
            name, private_body = read_function(generated, "0x02001000")
            self.assertEqual(name, "code_092.c")
            self.assertIn("goto L_02001018", private_body)
            with self.assertRaises(ValueError):
                read_function(generated, "0x02002000")


if __name__ == "__main__":
    unittest.main()
