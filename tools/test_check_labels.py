# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Tests of tools/check_labels.py. Run from the repository root:

    python -m unittest discover -s tools -p "test_*.py"
"""

import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_labels  # noqa: E402
import repo_files  # noqa: E402

VALID_LABELS = ["bug", "size: small", "priority: low"]


def event_of(*labels):
    return {"pull_request": {"labels": [{"name": name} for name in labels]}}


class CheckLabelsTest(unittest.TestCase):

    def test_valid_single_labels(self):
        self.assertEqual(check_labels.check_labels(["bug", "size: small", "priority: low"]), [])

    def test_valid_two_type_and_extra_labels(self):
        labels = ["bug", "enhancement", "size: medium", "priority: high", "help wanted", "needs-triage"]
        self.assertEqual(check_labels.check_labels(labels), [])

    def test_missing_type(self):
        self.assertEqual(check_labels.check_labels(["size: small", "priority: low"]), ["type: missing"])

    def test_missing_size(self):
        self.assertEqual(check_labels.check_labels(["bug", "priority: low"]), ["size: missing"])

    def test_missing_priority(self):
        self.assertEqual(check_labels.check_labels(["bug", "size: small"]), ["priority: missing"])

    def test_more_than_one_size(self):
        labels = ["bug", "size: small", "size: large", "priority: low"]
        self.assertEqual(
            check_labels.check_labels(labels),
            ["size: more than one (size: small, size: large)"],
        )

    def test_more_than_one_priority(self):
        labels = ["bug", "size: small", "priority: low", "priority: high"]
        self.assertEqual(
            check_labels.check_labels(labels),
            ["priority: more than one (priority: low, priority: high)"],
        )

    def test_no_labels(self):
        self.assertEqual(
            check_labels.check_labels([]),
            ["type: missing", "size: missing", "priority: missing"],
        )

    def test_labels_outside_the_groups_do_not_count(self):
        labels = ["duplicate", "invalid", "wontfix", "good first issue", "help wanted", "needs-triage"]
        self.assertEqual(
            check_labels.check_labels(labels),
            ["type: missing", "size: missing", "priority: missing"],
        )


class EventLabelsTest(unittest.TestCase):

    def test_pull_request_event(self):
        event = {"pull_request": {"labels": [{"name": "bug"}, {"name": "size: small"}]}}
        self.assertEqual(check_labels.event_labels(event), ["bug", "size: small"])

    def test_issue_event(self):
        event = {"issue": {"labels": [{"name": "priority: high"}]}}
        self.assertEqual(check_labels.event_labels(event), ["priority: high"])

    def test_event_without_labels_is_an_error(self):
        with self.assertRaises(repo_files.CheckError):
            check_labels.event_labels({"number": 7})

    def test_read_event_file(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "event.json")
            with open(path, "w", encoding="utf-8") as handle:
                json.dump({"pull_request": {"labels": [{"name": "bug"}]}}, handle)
            self.assertEqual(check_labels.event_labels(check_labels.read_event(path)), ["bug"])


class MainTest(unittest.TestCase):

    def run_main(self, argv):
        output = io.StringIO()
        errors = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            code = check_labels.main(argv)
        return code, output.getvalue(), errors.getvalue()

    def write_event(self, folder, event):
        path = os.path.join(folder, "event.json")
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(event, handle)
        return path

    def test_check_valid_returns_zero(self):
        with tempfile.TemporaryDirectory() as folder:
            path = self.write_event(folder, event_of(*VALID_LABELS))
            code, output, errors = self.run_main(["--event", path, "--check"])
        self.assertEqual(code, 0)
        self.assertEqual(output, "")
        self.assertEqual(errors, "")

    def test_check_invalid_returns_one(self):
        with tempfile.TemporaryDirectory() as folder:
            path = self.write_event(folder, event_of("bug", "size: small"))
            code, output, errors = self.run_main(["--event", path, "--check"])
        self.assertEqual(code, 1)
        self.assertIn("priority: missing", output)

    def test_triage_true_returns_zero(self):
        with tempfile.TemporaryDirectory() as folder:
            path = self.write_event(folder, event_of("bug", "size: small"))
            code, output, errors = self.run_main(["--event", path, "--triage"])
        self.assertEqual(code, 0)
        self.assertEqual(output, "needs-triage=true\n")

    def test_triage_false_returns_zero(self):
        with tempfile.TemporaryDirectory() as folder:
            path = self.write_event(folder, event_of(*VALID_LABELS))
            code, output, errors = self.run_main(["--event", path, "--triage"])
        self.assertEqual(code, 0)
        self.assertEqual(output, "needs-triage=false\n")

    def test_no_event_path_returns_two(self):
        with mock.patch.dict(os.environ, {"GITHUB_EVENT_PATH": ""}):
            code, output, errors = self.run_main([])
        self.assertEqual(code, 2)
        self.assertIn("GITHUB_EVENT_PATH", errors)

    def test_event_without_labels_returns_two(self):
        with tempfile.TemporaryDirectory() as folder:
            path = self.write_event(folder, {"number": 7})
            code, output, errors = self.run_main(["--event", path])
        self.assertEqual(code, 2)
        self.assertIn("neither", errors)

    def test_missing_event_file_returns_two(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "missing.json")
            code, output, errors = self.run_main(["--event", path])
        self.assertEqual(code, 2)
        self.assertIn("read event", errors)


if __name__ == "__main__":
    unittest.main()
