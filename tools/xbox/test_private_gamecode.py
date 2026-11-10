#!/usr/bin/env python3
"""Game-free unit tests for private recompilation inventory."""
import json
from pathlib import Path
import tempfile
import unittest

import private_gamecode as prep


class PrivateRecompTest(unittest.TestCase):
    def test_repo_guard(self):
        self.assertTrue(prep.inside_repo(prep.ROOT / "game"))
        with tempfile.TemporaryDirectory() as folder:
            self.assertFalse(prep.inside_repo(folder))
            with self.assertRaises(ValueError):
                prep.save_summary(prep.ROOT / "game" / "xbox.json", {})

    def test_inventory_and_bad_metadata(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "funcs.h").write_text("#pragma once\n#include \"ppc.h\"\n")
            (root / "code_000.c").write_text(
                "#include \"funcs.h\"\nvoid f_02000000(Cpu* c) { c->r[3] = 42; }\n")
            (root / "table.c").write_text(
                "const uint32_t g_recomp_entry_point = 0x02000000u;\n"
                "const unsigned g_recomp_func_count = 1;\n"
                "const unsigned g_recomp_import_count = 0;\n")
            (root / "imports.json").write_text("[]\n")
            meta = prep.inspect_generated(root)
            self.assertEqual(meta["entry_point"], "0x02000000")
            self.assertEqual(meta["function_count"], 1)
            self.assertEqual(meta["import_count"], 0)
            self.assertEqual(meta["code_files"], 1)
            self.assertFalse(meta["game_code_executed_on_xbox"])
            self.assertFalse(meta["native_xbox_linked"])
            # Saving a harmless summary is permitted outside the repo.
            output = root / "summary.json"
            prep.save_summary(output, meta)
            self.assertEqual(json.loads(output.read_text())["function_count"], 1)
            with self.assertRaises(FileExistsError):
                prep.save_summary(output, meta)
            (root / "imports.json").write_text('[{"slot": 100}]\n')
            with self.assertRaisesRegex(ValueError, "disagrees"):
                prep.inspect_generated(root)
            (root / "imports.json").write_text("[]\n")
            (root / "table.c").write_text("const unsigned g_recomp_func_count = 1;\n")
            with self.assertRaisesRegex(ValueError, "entry_point"):
                prep.inspect_generated(root)

if __name__ == "__main__":
    unittest.main()
