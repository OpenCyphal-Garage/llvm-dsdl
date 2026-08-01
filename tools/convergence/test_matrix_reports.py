#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Regression tests for parity/malformed/determinism matrix report scripts."""

from __future__ import annotations

import argparse
import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Dict, List


def _load_json(path: Path) -> Dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def _write_json(path: Path, data: Dict[str, object]) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=False) + "\n", encoding="utf-8")


class MatrixReportRegressionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.repo_root = Path(self.repo_root_arg).resolve()
        self.ctest_test_dir = Path(self.ctest_test_dir_arg).resolve()
        self.ctest_config = self.ctest_config_arg

        self.parity_script = self.repo_root / "tools/convergence/parity_matrix_report.py"
        self.malformed_script = self.repo_root / "tools/convergence/malformed_contract_matrix_report.py"
        self.determinism_script = self.repo_root / "tools/convergence/determinism_matrix_report.py"

        self.parity_baseline = self.repo_root / "tools/convergence/parity_matrix_baseline.json"
        self.malformed_baseline = self.repo_root / "tools/convergence/malformed_contract_matrix_baseline.json"
        self.determinism_baseline = self.repo_root / "tools/convergence/determinism_matrix_baseline.json"

        for path in [
            self.parity_script,
            self.malformed_script,
            self.determinism_script,
            self.parity_baseline,
            self.malformed_baseline,
            self.determinism_baseline,
        ]:
            self.assertTrue(path.exists(), f"missing required test path: {path}")

    def _run_report(
        self,
        *,
        script_path: Path,
        baseline_path: Path,
        output_json: Path,
        output_md: Path,
    ) -> subprocess.CompletedProcess[str]:
        cmd = [
            sys.executable,
            str(script_path),
            "--repo-root",
            str(self.repo_root),
            "--ctest-test-dir",
            str(self.ctest_test_dir),
            "--output-json",
            str(output_json),
            "--output-md",
            str(output_md),
            "--baseline",
            str(baseline_path),
            "--check-regressions",
        ]
        if self.ctest_config:
            cmd.extend(["--ctest-config", self.ctest_config])
        return subprocess.run(cmd, text=True, capture_output=True, check=False)

    def test_parity_baseline_passes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-parity-selftest-") as tmp_dir:
            result = self._run_report(
                script_path=self.parity_script,
                baseline_path=self.parity_baseline,
                output_json=Path(tmp_dir) / "report.json",
                output_md=Path(tmp_dir) / "report.md",
            )
        self.assertEqual(result.returncode, 0, msg=f"parity baseline should pass, stderr={result.stderr}")

    def test_parity_coverage_regression_is_detected(self) -> None:
        baseline = copy.deepcopy(_load_json(self.parity_baseline))
        baseline["expected_covered"]["cpp"]["scalar"] = False
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-parity-selftest-") as tmp_dir:
            bad_baseline = Path(tmp_dir) / "baseline.json"
            _write_json(bad_baseline, baseline)
            result = self._run_report(
                script_path=self.parity_script,
                baseline_path=bad_baseline,
                output_json=Path(tmp_dir) / "report.json",
                output_md=Path(tmp_dir) / "report.md",
            )
        self.assertNotEqual(result.returncode, 0, msg="expected parity regression failure")
        self.assertIn("parity matrix regression:", result.stderr)
        self.assertIn("coverage drift:", result.stderr)

    def test_malformed_baseline_passes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-malformed-selftest-") as tmp_dir:
            result = self._run_report(
                script_path=self.malformed_script,
                baseline_path=self.malformed_baseline,
                output_json=Path(tmp_dir) / "report.json",
                output_md=Path(tmp_dir) / "report.md",
            )
        self.assertEqual(result.returncode, 0, msg=f"malformed baseline should pass, stderr={result.stderr}")

    def test_malformed_guard_regression_is_detected(self) -> None:
        baseline = copy.deepcopy(_load_json(self.malformed_baseline))
        baseline["native_behavior_guards"]["required_pass"].append("nonexistent_guard")
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-malformed-selftest-") as tmp_dir:
            bad_baseline = Path(tmp_dir) / "baseline.json"
            _write_json(bad_baseline, baseline)
            result = self._run_report(
                script_path=self.malformed_script,
                baseline_path=bad_baseline,
                output_json=Path(tmp_dir) / "report.json",
                output_md=Path(tmp_dir) / "report.md",
            )
        self.assertNotEqual(result.returncode, 0, msg="expected malformed guard regression failure")
        self.assertIn("malformed-contract regression:", result.stderr)
        self.assertIn("native-behaviour guard missing from report: nonexistent_guard", result.stderr)

    def test_determinism_baseline_passes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-determinism-selftest-") as tmp_dir:
            result = self._run_report(
                script_path=self.determinism_script,
                baseline_path=self.determinism_baseline,
                output_json=Path(tmp_dir) / "report.json",
                output_md=Path(tmp_dir) / "report.md",
            )
        self.assertEqual(result.returncode, 0, msg=f"determinism baseline should pass, stderr={result.stderr}")

    def test_determinism_coverage_regression_is_detected(self) -> None:
        baseline = copy.deepcopy(_load_json(self.determinism_baseline))
        baseline["expected_covered"]["go"] = False
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-determinism-selftest-") as tmp_dir:
            bad_baseline = Path(tmp_dir) / "baseline.json"
            _write_json(bad_baseline, baseline)
            result = self._run_report(
                script_path=self.determinism_script,
                baseline_path=bad_baseline,
                output_json=Path(tmp_dir) / "report.json",
                output_md=Path(tmp_dir) / "report.md",
            )
        self.assertNotEqual(result.returncode, 0, msg="expected determinism regression failure")
        self.assertIn("determinism matrix regression:", result.stderr)
        self.assertIn("coverage drift:", result.stderr)


class BehavioralGateTest(unittest.TestCase):
    """JUnit-consuming behavioral gate tests. Self-contained (no configured build dir).

    These prove the core behavioral property the gates now enforce: a matrix cell is
    covered only if a matching test actually executed and passed — a failed or skipped
    test (or a missing results file) must NOT read as covered.
    """

    def setUp(self) -> None:
        self.repo_root = Path(self.repo_root_arg).resolve()
        self.parity_script = self.repo_root / "tools/convergence/parity_matrix_report.py"
        self.determinism_script = self.repo_root / "tools/convergence/determinism_matrix_report.py"

    def _run(self, script: Path, extra: List[str]) -> subprocess.CompletedProcess[str]:
        cmd = [sys.executable, str(script), "--repo-root", str(self.repo_root), *extra]
        return subprocess.run(cmd, text=True, capture_output=True, check=False)

    @staticmethod
    def _junit(tmp: str, cases: List[tuple]) -> Path:
        parts = ['<?xml version="1.0" encoding="UTF-8"?>', '<testsuite name="ctest">']
        for name, outcome in cases:
            if outcome == "fail":
                parts.append(f'<testcase name="{name}"><failure/></testcase>')
            elif outcome == "skip":
                parts.append(f'<testcase name="{name}"><skipped/></testcase>')
            else:
                parts.append(f'<testcase name="{name}"/>')
        parts.append("</testsuite>")
        path = Path(tmp) / "junit.xml"
        path.write_text("\n".join(parts), encoding="utf-8")
        return path

    # Concrete evidence test names taken from the parity/determinism matrices.
    PARITY_SCALAR_C = "llvmdsdl-signed-narrow-c-python-parity"
    MINIMAL_PARITY_BASELINE = {"expected_covered": {"c": {"scalar": True}}, "minimum_scores": {}}

    def test_behavioral_flag_is_set_with_junit(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            junit = self._junit(tmp, [(self.PARITY_SCALAR_C, "pass")])
            out_json = Path(tmp) / "report.json"
            res = self._run(self.parity_script, ["--ctest-junit", str(junit), "--output-json", str(out_json)])
            self.assertEqual(res.returncode, 0, msg=res.stderr)
            self.assertTrue(_load_json(out_json)["behavioral"])

    def test_passing_evidence_covers_cell(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            junit = self._junit(tmp, [(self.PARITY_SCALAR_C, "pass")])
            baseline = Path(tmp) / "baseline.json"
            _write_json(baseline, self.MINIMAL_PARITY_BASELINE)
            res = self._run(
                self.parity_script,
                ["--ctest-junit", str(junit), "--baseline", str(baseline), "--check-regressions"],
            )
            self.assertEqual(res.returncode, 0, msg=res.stderr)

    def test_failing_evidence_uncovers_cell(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            junit = self._junit(tmp, [(self.PARITY_SCALAR_C, "fail")])
            baseline = Path(tmp) / "baseline.json"
            _write_json(baseline, self.MINIMAL_PARITY_BASELINE)
            res = self._run(
                self.parity_script,
                ["--ctest-junit", str(junit), "--baseline", str(baseline), "--check-regressions"],
            )
            self.assertNotEqual(res.returncode, 0, msg="a failing evidence test must uncover its cell")
            self.assertIn("coverage drift", res.stderr)

    def test_skipped_evidence_uncovers_cell(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            junit = self._junit(tmp, [(self.PARITY_SCALAR_C, "skip")])
            baseline = Path(tmp) / "baseline.json"
            _write_json(baseline, self.MINIMAL_PARITY_BASELINE)
            res = self._run(
                self.parity_script,
                ["--ctest-junit", str(junit), "--baseline", str(baseline), "--check-regressions"],
            )
            self.assertNotEqual(res.returncode, 0, msg="a skipped evidence test must not read as covered")

    def test_missing_junit_file_is_loud(self) -> None:
        res = self._run(self.parity_script, ["--ctest-junit", "/nonexistent/does-not-exist.xml"])
        self.assertEqual(res.returncode, 2, msg="a missing results file must be a hard error, not a silent pass")
        self.assertIn("results file not found", res.stderr)

    def test_determinism_failing_backend_uncovered(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            junit = self._junit(
                tmp,
                [("llvmdsdl-uavcan-c-determinism", "pass"), ("llvmdsdl-uavcan-go-determinism", "fail")],
            )
            baseline = Path(tmp) / "baseline.json"
            _write_json(baseline, {"expected_covered": {"c": True, "go": True}, "minimum_scores": {}})
            res = self._run(
                self.determinism_script,
                ["--ctest-junit", str(junit), "--baseline", str(baseline), "--check-regressions"],
            )
            self.assertNotEqual(res.returncode, 0, msg="a failing determinism test must uncover its backend")
            self.assertIn("coverage drift", res.stderr)


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run matrix report regression tests.")
    parser.add_argument("--repo-root", required=True, help="Path to repository root.")
    parser.add_argument("--ctest-test-dir", required=True, help="Configured CTest build directory.")
    parser.add_argument("--ctest-config", help="CTest configuration (for multi-config generators).")
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    MatrixReportRegressionTest.repo_root_arg = args.repo_root
    MatrixReportRegressionTest.ctest_test_dir_arg = args.ctest_test_dir
    MatrixReportRegressionTest.ctest_config_arg = args.ctest_config
    BehavioralGateTest.repo_root_arg = args.repo_root

    loader = unittest.defaultTestLoader
    suite = unittest.TestSuite()
    suite.addTests(loader.loadTestsFromTestCase(MatrixReportRegressionTest))
    suite.addTests(loader.loadTestsFromTestCase(BehavioralGateTest))
    # Fold in the pure JUnit-parser unit tests so they run under the same gate.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    try:
        import test_ctest_results

        suite.addTests(loader.loadTestsFromModule(test_ctest_results))
    except Exception as exc:  # noqa: BLE001 - diagnostic only
        print(f"warning: could not load test_ctest_results: {exc}", file=sys.stderr)

    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

