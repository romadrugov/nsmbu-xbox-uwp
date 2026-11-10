#!/usr/bin/env python3
"""Game-free unit tests for the private Windows COFF compile probe."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import private_compile_one as probe
import private_gamecode


class PrivateCOFFTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.generated = self.root / "generated"
        self.generated.mkdir()
        (self.generated / "funcs.h").write_text('#pragma once\n#include "ppc.h"\n')
        (self.generated / "code_000.c").write_text(
            '#include "funcs.h"\nvoid f_02000000(Cpu* c) { c->r[3] = 42; }\n')
        (self.generated / "code_001.c").write_text(
            '#include "funcs.h"\nvoid f_02000004(Cpu* c) { c->r[3] = 43; }\n'
            '/* This file is longer than code_000.c */\n')
        (self.generated / "table.c").write_text(
            "const uint32_t g_recomp_entry_point = 0x02000000u;\n"
            "const unsigned g_recomp_func_count = 2;\n"
            "const unsigned g_recomp_import_count = 0;\n")
        (self.generated / "imports.json").write_text("[]\n")
        self.info = private_gamecode.inspect_generated(self.generated)
        self.info.update({
            "build": "USA_V0",
            "rpx_sha256": "ec31a785a16cb6ade4810f2ce8efe10da3501a1a5cf52e5fce8d8c16c54a5069",
            "build_verified_by_sha256": True,
            "source_data_kept_local": True,
        })
        self.summary = self.root / "summary.json"
        self.summary.write_text(json.dumps(self.info))

    def test_verify_private_manifest_and_select_smallest(self):
        verified = probe.verify_private_summary(self.generated, self.summary)
        self.assertEqual(verified["build"], "USA_V0")
        self.assertEqual(probe.choose_source(self.generated, None).name, "code_000.c")
        self.assertEqual(probe.choose_source(self.generated, "code_001.c").name, "code_001.c")
        for bad in ("../code_000.c", "table.c", "code_000.c/../x"):
            with self.assertRaises(ValueError):
                probe.choose_source(self.generated, bad)

    def test_reject_modified_manifest_and_inventory(self):
        altered = dict(self.info, code_files=123)
        self.summary.write_text(json.dumps(altered))
        with self.assertRaisesRegex(ValueError, "disagree"):
            probe.verify_private_summary(self.generated, self.summary)
        altered["code_files"] = 2
        altered["rpx_sha256"] = "0" * 64
        self.summary.write_text(json.dumps(altered))
        with self.assertRaisesRegex(ValueError, "SHA256"):
            probe.verify_private_summary(self.generated, self.summary)
        with self.assertRaises(ValueError):
            probe.verify_fixture(self.generated)

    def test_plan_works_without_windows_compiler(self):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = probe.main(["plan", "--generated", str(self.generated),
                               "--summary", str(self.summary)])
        self.assertEqual(code, 0)
        self.assertIn("Ready for compile", out.getvalue())
        self.assertFalse((self.root / "gamecode_probe.obj").exists())

    def test_coff_compile_guard_and_report(self):
        selected = probe.choose_source(self.generated, None)
        with self.assertRaisesRegex(ValueError, "outside|separate|public"):
            probe.compile_one(self.generated, selected, probe.ROOT / "build-private", "clang-cl")

        def fake_run(command, **kwargs):
            output = next(arg[3:] for arg in command if arg.startswith("/Fo"))
            Path(output).write_bytes(b"\x64\x86" + bytes(62))
            return subprocess.CompletedProcess(command, 0)
        with mock.patch.object(probe.subprocess, "run", side_effect=fake_run):
            output = self.root / "obj"
            obj = probe.compile_one(self.generated, selected, output, "clang-cl")
        self.assertEqual(obj.read_bytes()[:2], b"\x64\x86")
        self.assertTrue((output / "compile.log").is_file())
        with self.assertRaisesRegex(ValueError, "new or empty"):
            probe.compile_one(self.generated, selected, output, "clang-cl")

    def test_wrong_object_machine_rejected(self):
        selected = probe.choose_source(self.generated, None)
        def fake_run(command, **kwargs):
            output = next(arg[3:] for arg in command if arg.startswith("/Fo"))
            Path(output).write_bytes(b"\x7fELF" + bytes(60))
            return subprocess.CompletedProcess(command, 0)
        with mock.patch.object(probe.subprocess, "run", side_effect=fake_run):
            with self.assertRaisesRegex(RuntimeError, "not a Windows x64"):
                probe.compile_one(self.generated, selected, self.root / "bad", "clang-cl")


if __name__ == "__main__":
    unittest.main()
