# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Unit tests for tools/convergence/ctest_results.py."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ctest_results  # noqa: E402


def _write(tmp: Path, name: str, body: str) -> Path:
    path = tmp / name
    path.write_text(body, encoding="utf-8")
    return path


CTEST_JUNIT = """<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="ctest" tests="4">
  <testcase name="llvmdsdl-pass-one" classname="ctest" time="0.1"/>
  <testcase name="llvmdsdl-fail-one" classname="ctest" time="0.1">
    <failure message="boom">assertion failed</failure>
  </testcase>
  <testcase name="llvmdsdl-error-one" classname="ctest" time="0.1">
    <error message="err">crash</error>
  </testcase>
  <testcase name="llvmdsdl-skip-one" classname="ctest" time="0.0">
    <skipped message="toolchain missing"/>
  </testcase>
</testsuite>
"""

STATUS_ATTR_JUNIT = """<?xml version="1.0" encoding="UTF-8"?>
<testsuites>
  <testsuite name="s">
    <testcase name="llvmdsdl-status-notrun" status="notrun"/>
    <testcase name="llvmdsdl-status-failed" status="failed"/>
    <testcase name="llvmdsdl-status-pass"/>
  </testsuite>
</testsuites>
"""


class LoadJunitTests(unittest.TestCase):
    def setUp(self) -> None:
        import tempfile

        self._tmpdir = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmpdir.name)

    def tearDown(self) -> None:
        self._tmpdir.cleanup()

    def test_classifies_pass_fail_error_skip(self) -> None:
        path = _write(self.tmp, "junit.xml", CTEST_JUNIT)
        results = ctest_results.load_junit_results([path])
        self.assertEqual(results.passed, {"llvmdsdl-pass-one"})
        self.assertEqual(results.failed, {"llvmdsdl-fail-one", "llvmdsdl-error-one"})
        self.assertEqual(results.skipped, {"llvmdsdl-skip-one"})
        self.assertEqual(results.total, 4)

    def test_status_attribute_fallback(self) -> None:
        path = _write(self.tmp, "status.xml", STATUS_ATTR_JUNIT)
        results = ctest_results.load_junit_results([path])
        self.assertEqual(results.passed, {"llvmdsdl-status-pass"})
        self.assertEqual(results.failed, {"llvmdsdl-status-failed"})
        self.assertEqual(results.skipped, {"llvmdsdl-status-notrun"})

    def test_precedence_fail_over_pass_over_skip(self) -> None:
        first = _write(
            self.tmp,
            "a.xml",
            '<testsuite><testcase name="t"/></testsuite>',  # pass
        )
        second = _write(
            self.tmp,
            "b.xml",
            '<testsuite><testcase name="t"><failure/></testcase>'
            '<testcase name="u"/></testsuite>',  # t fails, u passes
        )
        third = _write(
            self.tmp,
            "c.xml",
            '<testsuite><testcase name="u"><skipped/></testcase></testsuite>',  # u skipped
        )
        results = ctest_results.load_junit_results([first, second, third])
        # t appeared pass then fail -> fail wins.
        self.assertIn("t", results.failed)
        self.assertNotIn("t", results.passed)
        # u appeared pass then skip -> pass wins.
        self.assertIn("u", results.passed)
        self.assertNotIn("u", results.skipped)

    def test_missing_file_raises(self) -> None:
        with self.assertRaises(FileNotFoundError):
            ctest_results.load_junit_results([self.tmp / "nope.xml"])

    def test_bad_xml_raises(self) -> None:
        path = _write(self.tmp, "bad.xml", "<testsuite><oops")
        with self.assertRaises(ValueError):
            ctest_results.load_junit_results([path])

    def test_matches(self) -> None:
        names = ["llvmdsdl-uavcan-c-go-parity", "llvmdsdl-signed-narrow-c-ts-parity", "unrelated"]
        hit = ctest_results.matches(names, [r"^llvmdsdl-uavcan-c-go-parity$", r"^llvmdsdl-signed-narrow-c-ts"])
        self.assertEqual(hit, ["llvmdsdl-signed-narrow-c-ts-parity", "llvmdsdl-uavcan-c-go-parity"])


if __name__ == "__main__":
    unittest.main()
