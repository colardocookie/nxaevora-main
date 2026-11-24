# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Tests of tools/check_spdx.py. Run from the repository root:

    python -m unittest discover -s tools -p "test_*.py"
"""

import contextlib
import io
import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_spdx  # noqa: E402
import repo_files  # noqa: E402

HASH_HEADER = (
    b"# SPDX-License-Identifier: GPL-3.0-or-later\n"
    b"# Copyright 2026 the NXAevora contributors\n"
)
HTML_HEADER = (
    b"<!-- SPDX-License-Identifier: GPL-3.0-or-later -->\n"
    b"<!-- Copyright 2026 the NXAevora contributors -->\n"
)
SLASH_HEADER = (
    b"// SPDX-License-Identifier: GPL-3.0-or-later\n"
    b"// Copyright 2026 the NXAevora contributors\n"
)
BLOCK_HEADER = (
    b"/* SPDX-License-Identifier: GPL-3.0-or-later */\n"
    b"/* Copyright 2026 the NXAevora contributors */\n"
)

FILLER = b"# filler\n"


class GoodHeaderTest(unittest.TestCase):

    def test_hash_suffixes(self):
        for suffix in (".py", ".yml", ".yaml", ".sh", ".ps1", ".toml", ".cmake", ".patch", ".pchtxt"):
            with self.subTest(suffix=suffix):
                self.assertEqual(check_spdx.classify("src/a" + suffix, HASH_HEADER), [])

    def test_gitignore_name(self):
        self.assertEqual(check_spdx.classify(".gitignore", HASH_HEADER), [])

    def test_cmake_lists(self):
        self.assertEqual(check_spdx.classify("CMakeLists.txt", HASH_HEADER), [])

    def test_html_suffix(self):
        self.assertEqual(check_spdx.classify("docs/a.md", HTML_HEADER), [])

    def test_bom_hash(self):
        self.assertEqual(check_spdx.classify("src/a.py", b"\xef\xbb\xbf" + HASH_HEADER), [])

    def test_bom_html(self):
        self.assertEqual(check_spdx.classify("docs/a.md", b"\xef\xbb\xbf" + HTML_HEADER), [])

    def test_slash_suffixes(self):
        for suffix in (".c", ".h", ".cpp", ".hpp"):
            with self.subTest(suffix=suffix):
                self.assertEqual(check_spdx.classify("src/a" + suffix, SLASH_HEADER), [])

    def test_block_comment(self):
        self.assertEqual(check_spdx.classify("src/a.c", BLOCK_HEADER), [])

    def test_year_range(self):
        data = b"# SPDX-License-Identifier: GPL-3.0-or-later\n# Copyright 2026-2027 the NXAevora contributors\n"
        self.assertEqual(check_spdx.classify("src/a.py", data), [])

    def test_shebang_then_header(self):
        data = b"#!/usr/bin/env python3\n" + HASH_HEADER
        self.assertEqual(check_spdx.classify("src/a.py", data), [])


class BadHeaderTest(unittest.TestCase):

    def assert_reason(self, reasons, word):
        self.assertEqual(len(reasons), 1)
        self.assertIn(word, reasons[0])

    def test_missing_license(self):
        data = b"# Copyright 2026 the NXAevora contributors\n"
        self.assert_reason(check_spdx.classify("src/a.py", data), "license")

    def test_wrong_license(self):
        data = b"# SPDX-License-Identifier: MIT\n# Copyright 2026 the NXAevora contributors\n"
        self.assertEqual(check_spdx.classify("src/a.py", data),
                         ["license line does not name GPL-3.0-or-later"])

    def test_wrong_copyright(self):
        data = b"# SPDX-License-Identifier: GPL-3.0-or-later\n# Copyright 2026 someone else\n"
        self.assertEqual(check_spdx.classify("src/a.py", data),
                         ["copyright line is not 'Copyright <year> the NXAevora contributors'"])

    def test_multiline_block_comment(self):
        data = (
            b"/*\n"
            b" * SPDX-License-Identifier: GPL-3.0-or-later\n"
            b" */\n"
            b"// Copyright 2026 the NXAevora contributors\n"
        )
        self.assertEqual(check_spdx.classify("src/a.c", data),
                         ["missing license line SPDX-License-Identifier: GPL-3.0-or-later"])

    def test_missing_license_new_scope(self):
        data = b"# Copyright 2026 the NXAevora contributors\n"
        for path in ("recomp/patches/a.patch", "recomp/patches/a.pchtxt", ".gitignore"):
            with self.subTest(path=path):
                self.assert_reason(check_spdx.classify(path, data), "license")

    def test_missing_copyright(self):
        data = b"# SPDX-License-Identifier: GPL-3.0-or-later\n"
        self.assert_reason(check_spdx.classify("src/a.py", data), "copyright")

    def test_header_after_line_ten(self):
        data = FILLER * 10 + HASH_HEADER
        self.assertEqual(len(check_spdx.classify("src/a.py", data)), 2)

    def test_hash_form_in_markdown(self):
        data = b"# SPDX-License-Identifier: GPL-3.0-or-later\n# Copyright 2026 the NXAevora contributors\n"
        self.assertEqual(len(check_spdx.classify("docs/a.md", data)), 2)


class ScopeTest(unittest.TestCase):

    def test_engine_is_not_checked(self):
        self.assertEqual(check_spdx.classify("engine/src/a.py", b""), [])

    def test_unchecked_suffix_is_not_checked(self):
        self.assertEqual(check_spdx.classify("tools/a.txt", b""), [])

    def test_unchecked_name_is_not_checked(self):
        self.assertEqual(check_spdx.classify("tools/a.json", b""), [])


class MainTest(unittest.TestCase):

    def run_main(self, paths, files):
        output = io.StringIO()
        errors = io.StringIO()
        with mock.patch.object(check_spdx, "list_tracked_files", return_value=paths), \
                mock.patch.object(check_spdx, "read_file", side_effect=files.__getitem__), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            code = check_spdx.main()
        return code, output.getvalue(), errors.getvalue()

    def test_clean_returns_zero(self):
        code, output, errors = self.run_main(["src/a.py"], {"src/a.py": HASH_HEADER})
        self.assertEqual(code, 0)
        self.assertIn("1 files checked, 0 violations", output)
        self.assertEqual(errors, "")

    def test_violation_returns_one(self):
        data = b"# Copyright 2026 the NXAevora contributors\n"
        code, output, errors = self.run_main(["src/a.py"], {"src/a.py": data})
        self.assertEqual(code, 1)
        self.assertIn("src/a.py: missing license line", output)

    def test_internal_error_returns_two(self):
        error = repo_files.CheckError("git ls-files", "boom")
        output = io.StringIO()
        errors = io.StringIO()
        with mock.patch.object(check_spdx, "list_tracked_files", side_effect=error), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            code = check_spdx.main()
        self.assertEqual(code, 2)
        self.assertIn("git ls-files", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
