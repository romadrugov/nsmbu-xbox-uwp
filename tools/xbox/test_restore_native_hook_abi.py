#!/usr/bin/env python3
"""No copyrighted or game-derived inputs: synthetic R8 hook ABI tests."""
from pathlib import Path
import importlib.util
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("restore_native_hook_abi.py")
spec = importlib.util.spec_from_file_location("restore_hook", SCRIPT)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def fixture(root: Path, duplicate=False):
    src = root / "private_fixture"
    src.mkdir()
    (src / "funcs.h").write_text(
        "#pragma once\n#include \"ppc.h\"\n"
        "void f_024B47A8(Cpu* __restrict c);\n"
        "void f_026904D4(Cpu* __restrict c);\n", encoding="utf-8")
    (src / "table.c").write_text(
        "/* owned fixture */\n"
        "void* fns[] = {f_024B47A8, f_026904D4};\n", encoding="utf-8")
    (src / "imports.c").write_text("/* owned fixture */\n", encoding="utf-8")
    for i in range(93):
        data = "/* synthetic source: no game code */\n"
        if i == 0:
            data += ("void f_024B47A8(Cpu* __restrict c) {\n"
                     "  c->r[3] = 42;\n}\n"
                     "void f_026904D4(Cpu* __restrict c) {\n"
                     "  f_024B47A8(c);\n}\n")
        if i == 1 and duplicate:
            data += "void f_024B47A8(Cpu* __restrict c) { c->r[3] = 7; }\n"
        (src / f"code_{i:03d}.c").write_text(data, encoding="utf-8")
    return src


class HookABITest(unittest.TestCase):
    def test_wrapper_calls_hook_and_preserves_original_and_table(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            src = fixture(root)
            out = root / "hook_wrapped"
            count = mod.reconstruct(src, out, {"024B47A8", "026904D4"})
            self.assertEqual(count, 2)
            code = (out / "code_000.c").read_text()
            self.assertIn(
                "void f_024B47A8(Cpu* __restrict c) { hook_024B47A8(c); }", code)
            self.assertIn(
                "void f_024B47A8_orig(Cpu* __restrict c) {\n  c->r[3] = 42;", code)
            self.assertIn("f_024B47A8(c);", code)
            self.assertIn("void hook_024B47A8(Cpu* c);",
                          (out / "funcs.h").read_text())
            self.assertEqual((out / "table.c").read_bytes(),
                             (src / "table.c").read_bytes())
            self.assertNotIn("_orig", (src / "code_000.c").read_text())
            with self.assertRaisesRegex(ValueError, "already hooked"):
                mod.reconstruct(out, root / "again", {"024B47A8"})

    def test_missing_definition_refuses_before_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            src = fixture(root)
            out = root / "out"
            code = src / "code_000.c"
            code.write_text(code.read_text().replace(
                "void f_026904D4(Cpu* __restrict c) {",
                "void f_026904D4_removed(Cpu* __restrict c) {"))
            with self.assertRaisesRegex(ValueError, "exactly one"):
                mod.reconstruct(src, out, {"024B47A8", "026904D4"})
            self.assertFalse(out.exists())

    def test_truly_absent_base_disc_hook_is_skipped_without_stub(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            src = fixture(root)
            (src / "funcs.h").write_text(
                (src / "funcs.h").read_text().replace(
                    "void f_026904D4(Cpu* __restrict c);\\n", ""))
            (src / "table.c").write_text(
                (src / "table.c").read_text().replace(
                    ", f_026904D4", ""))
            (src / "code_000.c").write_text(
                "/* synthetic source: no game code */\\n"
                "void f_024B47A8(Cpu* __restrict c) {\\n  c->r[3] = 42;\\n}\\n")
            out = root / "result"
            count = mod.reconstruct(src, out, {"024B47A8", "026904D4"})
            self.assertEqual(count, 1)
            self.assertNotIn("f_026904D4_orig", (out / "funcs.h").read_text())
            self.assertNotIn("f_026904D4", (out / "code_000.c").read_text())
            self.assertIn("f_024B47A8_orig", (out / "code_000.c").read_text())

    def test_missing_from_header_but_still_in_dispatch_refuses(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            src = fixture(root)
            (src / "funcs.h").write_text(
                (src / "funcs.h").read_text().replace(
                    "void f_026904D4(Cpu* __restrict c);\\n", ""))
            with self.assertRaisesRegex(ValueError, "dispatch table"):
                mod.reconstruct(src, root / "out", {"024B47A8", "026904D4"})
            self.assertFalse((root / "out").exists())

    def test_missing_from_header_but_defined_in_code_refuses(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            src = fixture(root)
            (src / "funcs.h").write_text(
                (src / "funcs.h").read_text().replace(
                    "void f_026904D4(Cpu* __restrict c);\\n", ""))
            (src / "table.c").write_text(
                (src / "table.c").read_text().replace(
                    ", f_026904D4", ""))
            with self.assertRaisesRegex(ValueError, "still defined"):
                mod.reconstruct(src, root / "out", {"024B47A8", "026904D4"})
            self.assertFalse((root / "out").exists())

    def test_build_hook_data_parses(self):
        hooks, sites = mod.hook_addresses_for("USA_V0")
        self.assertGreater(len(hooks), 0)
        self.assertGreaterEqual(sites, 0)
        self.assertIn("024B47A8", hooks)
        with self.assertRaisesRegex(ValueError, "USA_V0"):
            mod.hook_addresses_for("USA")


if __name__ == "__main__":
    unittest.main()
