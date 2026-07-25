#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Tests for the Debian packaging checker's parsing and verdict logic.

These cover the half of check_deb_config.py that decides whether the packaging is
correct, using recorded container output. No Docker involved, so the assertions
stay testable on any host -- including the ones that cannot build a .deb at all,
which is precisely where a regression in this logic would otherwise go unnoticed.
"""

from __future__ import annotations

import unittest
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_deb_config import (  # noqa: E402
    evaluate,
    parse_report,
    split_sections,
)

# A recorded good run: both packages present, distinct synopses, pinned -dev
# dependency, no lintian tags, all tools on PATH.
GOOD_OUTPUT = """\
@@LLVMDSDL@@ meta
version=0.1.0
arch=arm64
@@LLVMDSDL@@ exists llvm-dsdl
yes
@@LLVMDSDL@@ control llvm-dsdl
Package: llvm-dsdl
Version: 0.1.0
Architecture: arm64
Maintainer: OpenCyphal Maintainers <maintainers@opencyphal.org>
Depends: libc6, libstdc++6
Description: DSDL frontend, MLIR lowerer, and multi-language code generator
 Compiles Cyphal DSDL definitions to C, C++, Rust, Go, TypeScript, and
 Python through an MLIR dialect shared by every backend.
@@LLVMDSDL@@ contents llvm-dsdl
./usr/bin/dsdlc
./usr/share/man/man1/dsdlc.1.gz
@@LLVMDSDL@@ lintian llvm-dsdl
@@LLVMDSDL@@ exists llvm-dsdl-dev
yes
@@LLVMDSDL@@ control llvm-dsdl-dev
Package: llvm-dsdl-dev
Version: 0.1.0
Architecture: arm64
Maintainer: OpenCyphal Maintainers <maintainers@opencyphal.org>
Depends: llvm-dsdl (= 0.1.0)
Description: Development files for llvm-dsdl
 Static libraries and headers.
@@LLVMDSDL@@ contents llvm-dsdl-dev
./usr/lib/libllvmdsdlSupport.a
@@LLVMDSDL@@ lintian llvm-dsdl-dev
@@LLVMDSDL@@ install
onpath=dsdlc
onpath=dsdl-opt
onpath=dsdld
@@LLVMDSDL@@ dependency
enforced=yes
"""


def _mutate(output: str, old: str, new: str) -> str:
    assert old in output, f"fixture does not contain {old!r}"
    return output.replace(old, new, 1)


class SplitSectionsTest(unittest.TestCase):
    def test_orders_and_names_sections(self) -> None:
        sections = split_sections(GOOD_OUTPUT)
        self.assertEqual(sections[0][0], "meta")
        self.assertEqual(sections[1][0], "exists llvm-dsdl")

    def test_ignores_preamble_before_first_marker(self) -> None:
        sections = split_sections("noise from apt\n" + GOOD_OUTPUT)
        self.assertEqual(sections[0][0], "meta")


class ParseReportTest(unittest.TestCase):
    def setUp(self) -> None:
        self.report = parse_report(GOOD_OUTPUT)

    def test_reads_meta(self) -> None:
        self.assertEqual(self.report.version, "0.1.0")
        self.assertEqual(self.report.arch, "arm64")

    def test_reads_control_fields(self) -> None:
        self.assertEqual(self.report.control["llvm-dsdl"]["Package"], "llvm-dsdl")
        self.assertEqual(
            self.report.control["llvm-dsdl-dev"]["Depends"], "llvm-dsdl (= 0.1.0)")

    def test_folds_continuation_lines_into_description(self) -> None:
        description = self.report.control["llvm-dsdl"]["Description"]
        self.assertTrue(description.startswith("DSDL frontend"))
        # The indented body lines belong to the same field, not to new fields.
        self.assertIn("Compiles Cyphal DSDL definitions", description)
        self.assertNotIn("Compiles", self.report.control["llvm-dsdl"].keys())

    def test_empty_lintian_section_means_no_tags(self) -> None:
        self.assertEqual(self.report.lintian["llvm-dsdl"], [])

    def test_reads_install_and_dependency_results(self) -> None:
        self.assertEqual(self.report.tools_on_path, ["dsdlc", "dsdl-opt", "dsdld"])
        self.assertTrue(self.report.dependency_enforced)


class EvaluateTest(unittest.TestCase):
    def test_good_run_has_no_failures(self) -> None:
        self.assertEqual(evaluate(parse_report(GOOD_OUTPUT)), [])

    def test_missing_package_fails(self) -> None:
        output = _mutate(GOOD_OUTPUT, "@@LLVMDSDL@@ exists llvm-dsdl\nyes", "@@LLVMDSDL@@ exists llvm-dsdl\nno")
        failures = evaluate(parse_report(output))
        self.assertTrue(any("was not produced" in f for f in failures))

    def test_lintian_tag_fails(self) -> None:
        output = _mutate(
            GOOD_OUTPUT,
            "@@LLVMDSDL@@ lintian llvm-dsdl\n",
            "@@LLVMDSDL@@ lintian llvm-dsdl\nE: llvm-dsdl: no-copyright-file\n")
        failures = evaluate(parse_report(output))
        self.assertTrue(any("lintian is not clean" in f for f in failures))
        self.assertTrue(any("no-copyright-file" in f for f in failures))

    def test_unpinned_dev_dependency_fails(self) -> None:
        output = _mutate(GOOD_OUTPUT, "Depends: llvm-dsdl (= 0.1.0)", "Depends: llvm-dsdl")
        failures = evaluate(parse_report(output))
        self.assertTrue(any("does not pin" in f for f in failures))

    def test_inert_version_pin_fails(self) -> None:
        output = _mutate(GOOD_OUTPUT, "enforced=yes", "enforced=no")
        failures = evaluate(parse_report(output))
        self.assertTrue(any("version pin is inert" in f for f in failures))

    def test_missing_tool_on_path_fails(self) -> None:
        output = _mutate(GOOD_OUTPUT, "onpath=dsdld\n", "")
        failures = evaluate(parse_report(output))
        self.assertTrue(any("dsdld" in f and "PATH" in f for f in failures))

    def test_shared_synopsis_fails(self) -> None:
        # The regression this guards: setting CPACK_PACKAGE_DESCRIPTION_SUMMARY
        # makes CPack prepend one global synopsis to every component, so the -dev
        # package advertises itself as the code generator.
        output = _mutate(
            GOOD_OUTPUT,
            "Description: Development files for llvm-dsdl",
            "Description: DSDL frontend, MLIR lowerer, and multi-language code generator")
        failures = evaluate(parse_report(output))
        self.assertTrue(any("share one synopsis" in f for f in failures))

    def test_control_version_drift_fails(self) -> None:
        output = _mutate(GOOD_OUTPUT, "Package: llvm-dsdl\nVersion: 0.1.0", "Package: llvm-dsdl\nVersion: 0.0.9")
        failures = evaluate(parse_report(output))
        self.assertTrue(any("control Version" in f for f in failures))


if __name__ == "__main__":
    unittest.main()
