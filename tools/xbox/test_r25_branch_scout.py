"""R2.5 scanner tests use publicly authored synthetic PPC C only."""
import json
from pathlib import Path
import tempfile
import unittest

import r25_branch_scout as scout

# Eight public PowerPC instruction annotations, one conditional branch,
# internal forward-only labels, one register output and an explicit BLR.
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

BAD_EXTERNAL = GOOD.replace("goto L_02001018", "goto L_02009999")
BAD_MEMORY = GOOD.replace("r3 += 8;", "r3 = ld32(c->r[5]);")
BAD_LOOP = GOOD.replace("goto L_0200101C", "PPC_LOOP(); goto L_02001000")


class TestR25Scout(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.gen = self.root / "generated"
        self.gen.mkdir()
        self.out = self.root / "private-r25"

    def test_forward_branch_is_review_candidate_not_executed(self):
        item, reason = scout.evaluate("02001000", "code_000.c", GOOD.split("{", 1)[1].rsplit("}", 1)[0], 8, 96)
        self.assertEqual(reason, "review_only")
        self.assertEqual(item["status"], "REVIEW_ONLY_DO_NOT_EXECUTE")
        self.assertEqual(item["conditional_ppc_branches"], 1)
        self.assertTrue(item["generated_fallthrough"])
        self.assertGreaterEqual(item["local_gotos"], 2)

    def test_external_branch_never_candidate(self):
        item, reason = scout.evaluate("02001000", "code_000.c", BAD_EXTERNAL.split("{", 1)[1].rsplit("}", 1)[0], 8, 96)
        self.assertIsNone(item)
        self.assertEqual(reason, "external_or_backward_goto")

    def test_later_backward_edge_never_candidate(self):
        # Edge from 0x02001020 back to label 0x02001018: the prior
        # first-branch-only heuristic incorrectly admitted this.
        backward = GOOD.replace(
            "return; /* 02001020: 4E800020 */",
            "goto L_02001018; /* 02001020: 4800FFF8 */\n"
            "    return; /* 02001024: 4E800020 */")
        item, reason = scout.evaluate(
            "02001000", "code_000.c",
            backward.split("{", 1)[1].rsplit("}", 1)[0], 8, 96)
        self.assertIsNone(item)
        self.assertEqual(reason, "external_or_backward_goto")

    def test_memory_never_candidate(self):
        item, reason = scout.evaluate("02001000", "code_000.c", BAD_MEMORY.split("{", 1)[1].rsplit("}", 1)[0], 8, 96)
        self.assertIsNone(item)
        self.assertEqual(reason, "memory_or_runtime_dependency")

    def test_loop_never_candidate(self):
        item, reason = scout.evaluate("02001000", "code_000.c", BAD_LOOP.split("{", 1)[1].rsplit("}", 1)[0], 8, 96)
        self.assertIsNone(item)
        self.assertEqual(reason, "indirect_or_loop_control")

    def test_metadata_only_and_no_side_effects(self):
        (self.gen / "code_000.c").write_text(GOOD + "\n" + BAD_MEMORY + "\n" + BAD_EXTERNAL)
        result = scout.scout(self.gen, self.out)
        self.assertEqual(result["functions_examined"], 3)
        self.assertEqual(result["review_candidates_total"], 1)
        self.assertFalse(result["code_executed"])
        self.assertFalse(result["private_source_extracted"])
        raw = (self.out / "r25_branch_scout.json").read_text()
        data = json.loads(raw)
        self.assertEqual(data["shortlist"][0]["address"], "0x02001000")
        self.assertNotIn("ld32", raw)
        self.assertNotIn("PPC_ENTER", raw)
        self.assertNotIn("MUSTTAIL", raw)
        self.assertFalse((self.out / "r25_selected.c").exists())


if __name__ == "__main__":
    unittest.main()
