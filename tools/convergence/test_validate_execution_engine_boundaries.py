#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Regression tests for execution-engine boundary validation."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import List


class ExecutionEngineBoundaryValidatorRegressionTest(unittest.TestCase):
    REQUIRED_PATHS = [
        "tools/convergence/validate_execution_engine_boundaries.py",
        "lib/CodeGen/emitter/C.cpp",
        "lib/CodeGen/emitter/Cpp.cpp",
        "lib/CodeGen/emitter/Rust.cpp",
        "lib/CodeGen/emitter/Go.cpp",
        "lib/CodeGen/emitter/Ts.cpp",
        "lib/CodeGen/emitter/Python.cpp",
        "test/integration/RunUavcanGeneration.cmake",
        "test/integration/RunUavcanCppGeneration.cmake",
        "test/integration/RunUavcanRustGeneration.cmake",
        "test/integration/RunUavcanGoGeneration.cmake",
        "test/integration/RunUavcanTsGeneration.cmake",
        "test/integration/RunUavcanPythonGeneration.cmake",
    ]

    def setUp(self) -> None:
        self.repo_root = Path(self.repo_root_arg).resolve()
        self.validator = self.repo_root / "tools/convergence/validate_execution_engine_boundaries.py"
        self.assertTrue(self.validator.exists(), f"missing validator script: {self.validator}")

    def _run_validator(self, repo_root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(repo_root / "tools/convergence/validate_execution_engine_boundaries.py"),
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
            self.assertTrue(src.exists(), f"missing required snapshot input: {src}")
            dst = snapshot_root / rel_path
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
        return snapshot_root

    def test_baseline_validator_passes(self) -> None:
        result = self._run_validator(self.repo_root)
        self.assertEqual(result.returncode, 0, msg=f"expected validator success, stderr={result.stderr}")

    def test_missing_native_skeleton_marker_is_detected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-exec-boundary-test-") as tmp_dir:
            snapshot = self._create_snapshot(Path(tmp_dir) / "snapshot")
            target = snapshot / "lib/CodeGen/emitter/Cpp.cpp"
            text = target.read_text(encoding="utf-8")
            self.assertIn("emitNativeFunctionSkeleton(", text)
            target.write_text(text.replace("emitNativeFunctionSkeleton(", "emitNativeFunctionSkeleton_REMOVED("),
                              encoding="utf-8")
            result = self._run_validator(snapshot)

        self.assertNotEqual(result.returncode, 0, msg="expected missing native skeleton marker regression failure")
        self.assertIn("execution-engine regression:", result.stderr)
        self.assertIn("missing required source marker", result.stderr)
        self.assertIn("lib/CodeGen/emitter/Cpp.cpp", result.stderr)

    def test_missing_fallback_guard_marker_is_detected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-exec-boundary-test-") as tmp_dir:
            snapshot = self._create_snapshot(Path(tmp_dir) / "snapshot")
            target = snapshot / "test/integration/RunUavcanTsGeneration.cmake"
            text = target.read_text(encoding="utf-8")
            marker = "set(found_backend_scalar_deser_fallback_signature FALSE)"
            self.assertIn(marker, text)
            target.write_text(text.replace(marker, "set(found_backend_scalar_deser_fallback_signature_REMOVED FALSE)", 1),
                              encoding="utf-8")
            result = self._run_validator(snapshot)

        self.assertNotEqual(result.returncode, 0, msg="expected fallback guard marker regression failure")
        self.assertIn("execution-engine regression:", result.stderr)
        self.assertIn("missing required guard marker", result.stderr)
        self.assertIn("RunUavcanTsGeneration.cmake", result.stderr)

    def test_forbidden_direct_render_ir_marker_is_detected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-exec-boundary-test-") as tmp_dir:
            snapshot = self._create_snapshot(Path(tmp_dir) / "snapshot")
            target = snapshot / "lib/CodeGen/emitter/Python.cpp"
            text = target.read_text(encoding="utf-8")
            text += "\n// regression injection: buildLoweredBodyRenderIR(\n"
            target.write_text(text, encoding="utf-8")
            result = self._run_validator(snapshot)

        self.assertNotEqual(result.returncode, 0, msg="expected forbidden marker regression failure")
        self.assertIn("execution-engine regression:", result.stderr)
        self.assertIn("forbidden source marker present", result.stderr)
        self.assertIn("lib/CodeGen/emitter/Python.cpp", result.stderr)

    def test_missing_uniform_preflight_contract_marker_is_detected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-exec-boundary-test-") as tmp_dir:
            snapshot = self._create_snapshot(Path(tmp_dir) / "snapshot")
            target = snapshot / "lib/CodeGen/emitter/Go.cpp"
            text = target.read_text(encoding="utf-8")
            marker = 'mlirSchemaCoverageValidationFailedForEmission("Go")'
            self.assertIn(marker, text)
            target.write_text(text.replace(marker, 'mlirSchemaCoverageValidationFailedForEmission_REMOVED("Go")', 1),
                              encoding="utf-8")
            result = self._run_validator(snapshot)

        self.assertNotEqual(result.returncode, 0, msg="expected missing uniform preflight marker regression failure")
        self.assertIn("execution-engine regression:", result.stderr)
        self.assertIn("missing required source marker", result.stderr)
        self.assertIn("lib/CodeGen/emitter/Go.cpp", result.stderr)


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run execution-engine boundary validator regression tests.")
    parser.add_argument("--repo-root", required=True, help="Path to repository root.")
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    ExecutionEngineBoundaryValidatorRegressionTest.repo_root_arg = args.repo_root
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ExecutionEngineBoundaryValidatorRegressionTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
