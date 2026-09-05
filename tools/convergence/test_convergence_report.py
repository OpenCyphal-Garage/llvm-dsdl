#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Regression tests for convergence report generation and baseline checks."""

from __future__ import annotations

import argparse
import json
import shutil
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


class ConvergenceReportRegressionTest(unittest.TestCase):
    CONVERGENCE_REPORT_REQUIRED_PATHS = [
        "lib/CodeGen/CEmitter.cpp",
        "lib/CodeGen/CppEmitter.cpp",
        "lib/CodeGen/RustEmitter.cpp",
        "lib/CodeGen/GoEmitter.cpp",
        "lib/CodeGen/TsEmitter.cpp",
        "lib/CodeGen/PythonEmitter.cpp",
        "test/integration/RunUavcanGeneration.cmake",
        "test/integration/RunUavcanCppGeneration.cmake",
        "test/integration/RunUavcanRustGeneration.cmake",
        "test/integration/RunUavcanGoGeneration.cmake",
        "test/integration/RunUavcanTsGeneration.cmake",
        "test/integration/RunUavcanPythonGeneration.cmake",
        "lib/IR/DSDLOps.cpp",
        "test/lit/lower-dsdl-serialization-missing-kind-contract.mlir",
        "test/lit/lower-dsdl-serialization-schema-empty-body-contract.mlir",
        "test/lit/lower-dsdl-serialization-lowered-envelope-contract.mlir",
        "test/lit/lower-dsdl-serialization-lowered-step-count-mismatch-contract.mlir",
        "test/lit/lower-dsdl-serialization-plan-missing-bounds-contract.mlir",
        "test/lit/lower-dsdl-serialization-unsupported-array-kind-contract.mlir",
        "test/lit/lower-dsdl-serialization-unsupported-cast-mode-contract.mlir",
        "test/lit/lower-dsdl-serialization-variable-array-prefix-width-contract.mlir",
        "test/lit/lower-dsdl-serialization-union-missing-metadata-contract.mlir",
        "lib/Transforms/LoweredSerDesContractValidation.cpp",
        "lib/CodeGen/MlirLoweredFacts.cpp",
        "lib/Transforms/BuildDSDLPlanBodies.cpp",
        "lib/Transforms/ConvertDSDLToEmitC.cpp",
        "include/llvmdsdl/CodeGen/WireOperationContract.h",
        "lib/CodeGen/LoweredRenderIR.cpp",
        "lib/CodeGen/RuntimeLoweredPlan.cpp",
        "lib/CodeGen/ScriptedOperationPlan.cpp",
        "test/unit/NativeHelperContractTests.cpp",
        "test/unit/HelperBodyPlanTests.cpp",
        "test/unit/UnitMain.cpp",
        "test/unit/CMakeLists.txt",
    ]

    def setUp(self) -> None:
        self.repo_root = Path(self.repo_root_arg).resolve()
        self.report_script = self.repo_root / "tools/convergence/convergence_report.py"
        self.baseline_path = self.repo_root / "tools/convergence/convergence_baseline.json"
        self.assertTrue(self.report_script.exists(), f"missing convergence report script: {self.report_script}")
        self.assertTrue(self.baseline_path.exists(), f"missing convergence baseline: {self.baseline_path}")

    def _create_convergence_repo_snapshot(self, snapshot_root: Path) -> Path:
        for rel_path in self.CONVERGENCE_REPORT_REQUIRED_PATHS:
            src = self.repo_root / rel_path
            self.assertTrue(src.exists(), f"missing required convergence input path: {src}")
            dst = snapshot_root / rel_path
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
        return snapshot_root

    def _run_report(
        self,
        *,
        baseline_path: Path | None,
        check_regressions: bool,
        output_json: Path,
        output_md: Path,
        repo_root: Path | None = None,
    ) -> subprocess.CompletedProcess[str]:
        cmd = [
            sys.executable,
            str(self.report_script),
            "--repo-root",
            str((repo_root or self.repo_root).resolve()),
            "--output-json",
            str(output_json),
            "--output-md",
            str(output_md),
        ]
        if baseline_path is not None:
            cmd.extend(["--baseline", str(baseline_path)])
        if check_regressions:
            cmd.append("--check-regressions")
        return subprocess.run(cmd, text=True, capture_output=True, check=False)

    def _assert_contract_v2_mutation_regression(
        self,
        *,
        relative_path: str,
        present_token: str,
        replacement_token: str,
    ) -> None:
        baseline = _load_json(self.baseline_path)

        with tempfile.TemporaryDirectory(prefix="llvmdsdl-convergence-test-") as tmp_dir:
            snapshot_root = self._create_convergence_repo_snapshot(Path(tmp_dir) / "snapshot")
            mutated_file = snapshot_root / relative_path
            original_text = mutated_file.read_text(encoding="utf-8")
            self.assertIn(present_token, original_text)
            mutated_file.write_text(
                original_text.replace(present_token, replacement_token),
                encoding="utf-8",
            )

            bad_baseline = Path(tmp_dir) / "baseline.json"
            output_json = Path(tmp_dir) / "report.json"
            output_md = Path(tmp_dir) / "report.md"
            _write_json(bad_baseline, baseline)
            result = self._run_report(
                baseline_path=bad_baseline,
                check_regressions=True,
                output_json=output_json,
                output_md=output_md,
                repo_root=snapshot_root,
            )

        self.assertNotEqual(result.returncode, 0, msg="expected contract_v2 regression failure")
        self.assertIn("convergence regression:", result.stderr)
        self.assertIn("contract_v2_usage", result.stderr)

    def _assert_classification_mutation_regression(
        self,
        *,
        relative_path: str,
        present_token: str,
        replacement_token: str,
        expected_class_id: str,
    ) -> None:
        baseline = _load_json(self.baseline_path)

        with tempfile.TemporaryDirectory(prefix="llvmdsdl-convergence-test-") as tmp_dir:
            snapshot_root = self._create_convergence_repo_snapshot(Path(tmp_dir) / "snapshot")
            mutated_file = snapshot_root / relative_path
            original_text = mutated_file.read_text(encoding="utf-8")
            self.assertIn(present_token, original_text)
            mutated_file.write_text(
                original_text.replace(present_token, replacement_token),
                encoding="utf-8",
            )

            bad_baseline = Path(tmp_dir) / "baseline.json"
            output_json = Path(tmp_dir) / "report.json"
            output_md = Path(tmp_dir) / "report.md"
            _write_json(bad_baseline, baseline)
            result = self._run_report(
                baseline_path=bad_baseline,
                check_regressions=True,
                output_json=output_json,
                output_md=output_md,
                repo_root=snapshot_root,
            )

        self.assertNotEqual(result.returncode, 0, msg=f"expected {expected_class_id} regression failure")
        self.assertIn("convergence regression:", result.stderr)
        self.assertIn(expected_class_id, result.stderr)

    def test_report_includes_new_architecture_dimensions(self) -> None:
        with tempfile.TemporaryDirectory(prefix="llvmdsdl-convergence-test-") as tmp_dir:
            output_json = Path(tmp_dir) / "report.json"
            output_md = Path(tmp_dir) / "report.md"
            result = self._run_report(
                baseline_path=self.baseline_path,
                check_regressions=True,
                output_json=output_json,
                output_md=output_md,
            )
            self.assertEqual(
                result.returncode,
                0,
                msg=f"expected report success, got rc={result.returncode}, stderr={result.stderr}",
            )
            report = _load_json(output_json)
        class_ids = {entry["id"] for entry in report["semantic_classes"]}
        self.assertIn("verifier_first_invariants", class_ids)
        self.assertIn("helper_binding_completeness", class_ids)
        self.assertIn("contract_v2_usage", class_ids)
        self.assertIn("fallback_free_semantic_execution", class_ids)
        for backend_name, backend in report["backends"].items():
            self.assertIn("verifier_first_invariants", backend["classifications"], msg=backend_name)
            self.assertIn("helper_binding_completeness", backend["classifications"], msg=backend_name)
            self.assertIn("contract_v2_usage", backend["classifications"], msg=backend_name)
            self.assertIn("fallback_free_semantic_execution", backend["classifications"], msg=backend_name)
            self.assertEqual(backend["score"], 100, msg=backend_name)
            self.assertEqual(backend["shared_classes"], backend["total_classes"], msg=backend_name)
            self.assertTrue(
                all(status == "shared" for status in backend["classifications"].values()),
                msg=backend_name,
            )
        self.assertEqual(report["project_floor_score"], 100)

    def test_baseline_missing_backend_is_detected(self) -> None:
        baseline = _load_json(self.baseline_path)
        baseline["expected"].pop("python")

        with tempfile.TemporaryDirectory(prefix="llvmdsdl-convergence-test-") as tmp_dir:
            bad_baseline = Path(tmp_dir) / "baseline.json"
            output_json = Path(tmp_dir) / "report.json"
            output_md = Path(tmp_dir) / "report.md"
            _write_json(bad_baseline, baseline)
            result = self._run_report(
                baseline_path=bad_baseline,
                check_regressions=True,
                output_json=output_json,
                output_md=output_md,
            )

        self.assertNotEqual(result.returncode, 0, msg="expected baseline-shape backend failure")
        self.assertIn("convergence regression:", result.stderr)
        self.assertIn("baseline missing required backends", result.stderr)

    def test_baseline_missing_class_is_detected(self) -> None:
        baseline = _load_json(self.baseline_path)
        baseline["expected"]["c"].pop("contract_v2_usage")

        with tempfile.TemporaryDirectory(prefix="llvmdsdl-convergence-test-") as tmp_dir:
            bad_baseline = Path(tmp_dir) / "baseline.json"
            output_json = Path(tmp_dir) / "report.json"
            output_md = Path(tmp_dir) / "report.md"
            _write_json(bad_baseline, baseline)
            result = self._run_report(
                baseline_path=bad_baseline,
                check_regressions=True,
                output_json=output_json,
                output_md=output_md,
            )

        self.assertNotEqual(result.returncode, 0, msg="expected baseline-shape class failure")
        self.assertIn("convergence regression:", result.stderr)
        self.assertIn("missing semantic classes", result.stderr)

    def test_baseline_non_shared_status_is_detected(self) -> None:
        baseline = _load_json(self.baseline_path)
        baseline["expected"]["go"]["helper_binding_completeness"] = "backend-local"

        with tempfile.TemporaryDirectory(prefix="llvmdsdl-convergence-test-") as tmp_dir:
            bad_baseline = Path(tmp_dir) / "baseline.json"
            output_json = Path(tmp_dir) / "report.json"
            output_md = Path(tmp_dir) / "report.md"
            _write_json(bad_baseline, baseline)
            result = self._run_report(
                baseline_path=bad_baseline,
                check_regressions=True,
                output_json=output_json,
                output_md=output_md,
            )

        self.assertNotEqual(result.returncode, 0, msg="expected baseline non-shared class failure")
        self.assertIn("convergence regression:", result.stderr)
        self.assertIn("must be 'shared'", result.stderr)

    def test_expected_shared_regression_is_detected(self) -> None:
        self._assert_classification_mutation_regression(
            relative_path="lib/Transforms/BuildDSDLPlanBodies.cpp",
            present_token="callErrorHelper(",
            replacement_token="callErrorHelper_REMOVED(",
            expected_class_id="malformed_input_diagnostic_text",
        )

    def test_project_floor_regression_is_detected(self) -> None:
        baseline = _load_json(self.baseline_path)
        baseline["minimum_scores"]["project_floor"] = 101

        with tempfile.TemporaryDirectory(prefix="llvmdsdl-convergence-test-") as tmp_dir:
            bad_baseline = Path(tmp_dir) / "baseline.json"
            output_json = Path(tmp_dir) / "report.json"
            output_md = Path(tmp_dir) / "report.md"
            _write_json(bad_baseline, baseline)
            result = self._run_report(
                baseline_path=bad_baseline,
                check_regressions=True,
                output_json=output_json,
                output_md=output_md,
            )

        self.assertNotEqual(result.returncode, 0, msg="expected project-floor score regression failure")
        self.assertIn("convergence regression:", result.stderr)
        self.assertIn("project floor", result.stderr)

    def test_contract_v2_helper_token_regression_is_detected(self) -> None:
        self._assert_contract_v2_mutation_regression(
            relative_path="lib/Transforms/ConvertDSDLToEmitC.cpp",
            present_token="loweredSerDesUnsupportedMajorVersionDiagnosticDetail",
            replacement_token="loweredSerDesUnsupportedContractMajorDiagnosticDetail",
        )

    def test_contract_v2_codegen_consumer_token_regression_is_detected(self) -> None:
        self._assert_contract_v2_mutation_regression(
            relative_path="lib/CodeGen/MlirLoweredFacts.cpp",
            present_token="loweredSerDesUnsupportedMajorVersionDiagnosticDetail",
            replacement_token="loweredSerDesUnsupportedContractMajorDiagnosticDetail",
        )

    def test_contract_v2_validation_api_token_regression_is_detected(self) -> None:
        self._assert_contract_v2_mutation_regression(
            relative_path="lib/Transforms/LoweredSerDesContractValidation.cpp",
            present_token="findLoweredPlanContractViolation",
            replacement_token="findLoweredPlanContractIssue",
        )

    def test_contract_v2_wire_operation_contract_token_regression_is_detected(self) -> None:
        self._assert_contract_v2_mutation_regression(
            relative_path="lib/CodeGen/RuntimeLoweredPlan.cpp",
            present_token="validateRuntimeSectionPlanContract",
            replacement_token="validateRuntimeSectionPlanMarker",
        )

    def test_verifier_first_invariants_token_regression_is_detected(self) -> None:
        self._assert_classification_mutation_regression(
            relative_path="lib/IR/DSDLOps.cpp",
            present_token="LogicalResult SerializationPlanOp::verify()",
            replacement_token="LogicalResult SerializationPlanOp::verifyContract()",
            expected_class_id="verifier_first_invariants",
        )

    def test_fallback_free_semantic_execution_token_regression_is_detected(self) -> None:
        self._assert_classification_mutation_regression(
            relative_path="test/integration/RunUavcanGoGeneration.cmake",
            present_token="found_backend_fallback_signature",
            replacement_token="found_backend_fallback_flag",
            expected_class_id="fallback_free_semantic_execution",
        )

    def test_helper_binding_completeness_token_regression_is_detected(self) -> None:
        self._assert_classification_mutation_regression(
            relative_path="test/unit/NativeHelperContractTests.cpp",
            present_token="missing scalar helper bindings",
            replacement_token="missing scalar helper checks",
            expected_class_id="helper_binding_completeness",
        )


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run convergence report regression tests.")
    parser.add_argument("--repo-root", required=True, help="Path to repository root.")
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    ConvergenceReportRegressionTest.repo_root_arg = args.repo_root
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ConvergenceReportRegressionTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
