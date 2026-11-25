# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Tests of tools/check_game_data.py. Run from the repository root:

    python -m unittest discover -s tools -p "test_*.py"
"""

import contextlib
import io
import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_game_data as guard  # noqa: E402
import repo_files  # noqa: E402

HEADER_LENGTH = 0x104
SHIFTED_OFFSET = 0x20
# Offsets of the container and program magics, written here apart from the table under test.
EXPECTED_MAGICS = (
    (0x0, b"PFS0"),
    (0x0, b"HFS0"),
    (0x0, b"NSO0"),
    (0x10, b"NRO0"),
    (0x100, b"HEAD"),
)


def magic_header(offset, signature):
    data = bytearray(HEADER_LENGTH)
    data[offset:offset + len(signature)] = signature
    return bytes(data)


class ForbiddenPathTest(unittest.TestCase):

    def test_forbidden_suffixes(self):
        for suffix in guard.FORBIDDEN_SUFFIXES:
            with self.subTest(suffix=suffix):
                self.assertNotEqual(guard.classify_path("dumps/x" + suffix), [])
                self.assertNotEqual(guard.classify_path("dumps/x" + suffix.upper()), [])

    def test_forbidden_names(self):
        for name in guard.FORBIDDEN_NAMES:
            with self.subTest(name=name):
                self.assertNotEqual(guard.classify_path("keys/" + name), [])
                self.assertNotEqual(guard.classify_path("keys/" + name.upper()), [])

    def test_game_folder_first_part(self):
        self.assertNotEqual(guard.classify_path("game/x.txt"), [])

    def test_romfs_folder_anywhere(self):
        self.assertNotEqual(guard.classify_path("a/RomFS/b.bin"), [])

    def test_exefs_folder_anywhere(self):
        self.assertNotEqual(guard.classify_path("a/exefs/main"), [])

    def test_engine_source_passes(self):
        self.assertEqual(guard.classify_path("engine/src/fs/romfs.c"), [])

    def test_forbidden_names_exact_reason(self):
        for name in ("prod.keys", "main.npdm"):
            for path in ("keys/" + name, "keys/" + name.upper()):
                with self.subTest(path=path):
                    self.assertIn("forbidden file name " + path.rsplit("/", 1)[-1],
                                  guard.classify_path(path))

    def test_python_cache(self):
        self.assertNotEqual(guard.classify_path("__pycache__/x.pyc"), [])

    def test_python_cache_outside_pycache(self):
        self.assertIn("python cache file", guard.classify_path("tools/x.pyc"))


class MagicTest(unittest.TestCase):

    def test_each_magic_at_its_offset_fails(self):
        for offset, signature in EXPECTED_MAGICS:
            with self.subTest(signature=signature):
                self.assertNotEqual(guard.classify_magic(magic_header(offset, signature)), [])

    def test_each_magic_at_another_offset_passes(self):
        for offset, signature in EXPECTED_MAGICS:
            with self.subTest(signature=signature):
                self.assertEqual(guard.classify_magic(magic_header(SHIFTED_OFFSET, signature)), [])


class KeyTest(unittest.TestCase):

    KEY_NAMES = (
        "header_key",
        "key_area_key_application_00",
        "titlekek_00",
        "master_key_00",
        "package2_key_00",
        "eticket_rsa_kek",
        "bis_key_00",
        "keyblob_key_00",
        "tsec_root_key_00",
        "aes_kek_generation_source",
        "header_kek_source",
    )

    def test_each_key_name_with_32_digits_fails(self):
        for name in self.KEY_NAMES:
            with self.subTest(name=name):
                data = (name + " = " + "0" * 32).encode("ascii")
                self.assertNotEqual(guard.classify_keys(data), [])

    def test_upper_case_name_fails(self):
        data = b"HEADER_KEY = " + b"0" * 32
        self.assertNotEqual(guard.classify_keys(data), [])

    def test_indented_line_fails(self):
        data = b"    header_key = " + b"0" * 32
        self.assertNotEqual(guard.classify_keys(data), [])

    def test_name_without_key_or_kek_passes(self):
        data = b"build_id = " + b"0" * 32
        self.assertEqual(guard.classify_keys(data), [])

    def test_key_line_with_31_digits_passes(self):
        data = b"header_key = " + b"0" * 31
        self.assertEqual(guard.classify_keys(data), [])


class BinaryRangeTest(unittest.TestCase):

    def test_text_is_not_binary(self):
        self.assertFalse(guard.is_binary(b"plain text"))

    def test_nul_is_binary(self):
        self.assertTrue(guard.is_binary(b"text\0more"))

    def test_invalid_utf8_is_binary(self):
        self.assertTrue(guard.is_binary(b"\xff\xff"))


class XciHeadTest(unittest.TestCase):

    def test_text_with_head_at_0x100_passes(self):
        data = bytearray(b"a" * HEADER_LENGTH)
        data[0x100:0x104] = b"HEAD"
        self.assertEqual(guard.classify_magic(bytes(data)), [])

    def test_zeros_with_head_at_0x100_fail(self):
        self.assertNotEqual(guard.classify_magic(magic_header(0x100, b"HEAD")), [])

    def test_invalid_utf8_with_head_at_0x100_fails(self):
        data = bytearray(b"\xff" * HEADER_LENGTH)
        data[0x100:0x104] = b"HEAD"
        self.assertNotEqual(guard.classify_magic(bytes(data)), [])


class SizeTest(unittest.TestCase):

    def test_exactly_five_mebibytes_passes(self):
        self.assertEqual(guard.classify_size(5 * 1024 * 1024), [])

    def test_five_mebibytes_plus_one_fails(self):
        self.assertNotEqual(guard.classify_size(5 * 1024 * 1024 + 1), [])


class ExcludedTest(unittest.TestCase):

    def test_excluded_path_passes(self):
        path = "assets/romfs/x.otf"
        self.assertNotEqual(guard.classify(path, b"", 0), [])
        with mock.patch.dict(guard.EXCLUDED_FILES, {path: "a font, not game data"}):
            self.assertEqual(guard.classify(path, b"", 0), [])

    def test_combined_checks(self):
        self.assertNotEqual(guard.classify("dumps/x.nsp", b"", 0), [])


class MainTest(unittest.TestCase):

    def run_main(self, paths, files):
        output = io.StringIO()
        errors = io.StringIO()
        with mock.patch.object(guard, "list_tracked_files", return_value=paths), \
                mock.patch.object(guard, "read_file", side_effect=files.__getitem__), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            code = guard.main()
        return code, output.getvalue(), errors.getvalue()

    def test_clean_returns_zero(self):
        code, output, errors = self.run_main(["tools/a.txt"], {"tools/a.txt": (0, b"")})
        self.assertEqual(code, 0)
        self.assertIn("1 files checked, 0 violations", output)
        self.assertEqual(errors, "")

    def test_violation_returns_one(self):
        code, output, errors = self.run_main(["dumps/x.nsp"], {"dumps/x.nsp": (0, b"")})
        self.assertEqual(code, 1)
        self.assertIn("dumps/x.nsp: forbidden suffix .nsp", output)

    def test_internal_error_returns_two(self):
        error = repo_files.CheckError("git ls-files", "boom")
        output = io.StringIO()
        errors = io.StringIO()
        with mock.patch.object(guard, "list_tracked_files", side_effect=error), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            code = guard.main()
        self.assertEqual(code, 2)
        self.assertIn("git ls-files", errors.getvalue())

    def test_missing_file_returns_two(self):
        output = io.StringIO()
        errors = io.StringIO()
        with mock.patch.object(guard, "list_tracked_files", return_value=["tools/missing.txt"]), \
                mock.patch.object(guard.os.path, "getsize", side_effect=FileNotFoundError("gone")), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            code = guard.main()
        self.assertEqual(code, 2)
        self.assertIn("tools/missing.txt", errors.getvalue())
        self.assertIn("tracked file is missing from the working tree", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
