# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Tests of tools/repo_files.py. Run from the repository root:

    python -m unittest discover -s tools -p "test_*.py"
"""

import io
import os
import sys
import unittest
from contextlib import redirect_stdout
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import repo_files  # noqa: E402


class ReportTest(unittest.TestCase):

    def test_clean(self):
        output = io.StringIO()
        with redirect_stdout(output):
            code = repo_files.report([], 3)
        self.assertEqual(code, 0)
        self.assertIn("3 files checked, 0 violations", output.getvalue())

    def test_one_violation(self):
        output = io.StringIO()
        with redirect_stdout(output):
            code = repo_files.report([("a.py", "missing copyright line")], 1)
        text = output.getvalue()
        self.assertEqual(code, 1)
        self.assertIn("a.py: missing copyright line", text)
        self.assertIn("1 files checked, 1 violations", text)


class ListTrackedFilesTest(unittest.TestCase):

    def test_non_zero_exit(self):
        result = mock.Mock(returncode=1, stdout=b"", stderr=b"fatal: not a repository")
        with mock.patch.object(repo_files.subprocess, "run", return_value=result):
            with self.assertRaises(repo_files.CheckError) as caught:
                repo_files.list_tracked_files()
        self.assertEqual(caught.exception.stage, "git ls-files")

    def test_os_error(self):
        with mock.patch.object(repo_files.subprocess, "run", side_effect=OSError("boom")):
            with self.assertRaises(repo_files.CheckError) as caught:
                repo_files.list_tracked_files()
        self.assertEqual(caught.exception.stage, "git ls-files")

    def test_splits_nul_output(self):
        result = mock.Mock(returncode=0, stdout=b"a\0b/c\0", stderr=b"")
        with mock.patch.object(repo_files.subprocess, "run", return_value=result):
            self.assertEqual(repo_files.list_tracked_files(), ["a", "b/c"])


if __name__ == "__main__":
    unittest.main()
