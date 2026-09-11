#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Regression tests for hard-cut integrity validation."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import List


class HardCutIntegrityRegressionTest(unittest.TestCase):
    REQUIRED_PATHS = [
        "tools/convergence/validate_hard_cut_integrity.py",
        "test/integration/CMakeLists.txt",
        "lib/CodeGen/BodyTranslator.cpp",
        "lib/CodeGen/CodegenDiagnosticText.cpp",
        "include/llvmdsdl/Transforms/LoweredSerDesContractValidation.h",
        "lib/Transforms/LoweredSerDesContractValidation.cpp",
    ]

    def setUp(self) -> None:
        self.repo_root = Path(self.repo_root_arg).resolve()
        self.validator = self.repo_root / "tools/convergence/validate_hard_cut_integrity.py"
        self.assertTrue(self.validator.exists(), f"missing validator script: {self.validator}")

    def _run_validator(self, repo_root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(repo_root / "tools/convergence/validate_hard_cut_integrity.py"),
                "--repo-root",
                str(repo_root),
            ],
            text=True,
            capture_output=True,
            check=False,
        )

    def _create_snapshot(self, snapshot_root: Path) -> Path:
        for rel_path in self.REQUIRED_PATHS:
            src = self.repo_root / rel_path
            self.assertTrue(src.exists(), f"missing required hard-cut input path: {src}")
            dst = snapshot_root / rel_path
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
        return snapshot_root

    def test_baseline_passes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-hard-cut-test-") as tmp_dir:
            snapshot_root = self._create_snapshot(Path(tmp_dir) / "snapshot")
            result = self._run_validator(snapshot_root)
        self.assertEqual(
            result.returncode,
            0,
            msg=f"expected validator success, got rc={result.returncode}, stderr={result.stderr}",
        )

    def test_missing_canonical_marker_is_detected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-hard-cut-test-") as tmp_dir:
            snapshot_root = self._create_snapshot(Path(tmp_dir) / "snapshot")
            target = snapshot_root / "lib/CodeGen/BodyTranslator.cpp"
            text = target.read_text(encoding="utf-8")
            marker = "translateFunction("
            self.assertIn(marker, text)
            target.write_text(text.replace(marker, "translateFunction_REMOVED(", 1), encoding="utf-8")
            result = self._run_validator(snapshot_root)
        self.assertNotEqual(result.returncode, 0, msg="expected canonical marker regression failure")
        self.assertIn("required canonical marker missing", result.stderr)
        self.assertIn("BodyTranslator.cpp", result.stderr)

    def test_forbidden_shim_phrase_is_detected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-hard-cut-test-") as tmp_dir:
            snapshot_root = self._create_snapshot(Path(tmp_dir) / "snapshot")
            target = snapshot_root / "lib/CodeGen/CodegenDiagnosticText.cpp"
            text = target.read_text(encoding="utf-8")
            target.write_text(text + "\n// temporary compatibility shim layer for migration\n", encoding="utf-8")
            result = self._run_validator(snapshot_root)
        self.assertNotEqual(result.returncode, 0, msg="expected forbidden-phrase regression failure")
        self.assertIn("forbidden hard-cut phrase detected", result.stderr)
        self.assertIn("CodegenDiagnosticText.cpp", result.stderr)


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run hard-cut integrity validator regression tests.")
    parser.add_argument("--repo-root", required=True, help="Path to repository root.")
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    HardCutIntegrityRegressionTest.repo_root_arg = args.repo_root
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(HardCutIntegrityRegressionTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
