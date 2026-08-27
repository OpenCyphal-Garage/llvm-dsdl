#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Generate and gate backend determinism-matrix coverage from integration lanes."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Set

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ctest_results  # noqa: E402

MATRIX: Dict[str, List[str]] = {
    "c": [r"^llvmdsdl-uavcan-c-determinism$"],
    "cpp": [r"^llvmdsdl-uavcan-cpp-determinism$"],
    "rust": [r"^llvmdsdl-uavcan-rust-determinism-inline-then-pool$"],
    "go": [r"^llvmdsdl-uavcan-go-determinism$"],
    "ts": [r"^llvmdsdl-uavcan-ts-determinism$"],
    "python": [r"^llvmdsdl-uavcan-python-determinism$"],
    # The obj backend was absent from this table until its archive turned out to be
    # nondeterministic, which the report could not have reported: a backend that is not listed
    # here is not "covered", it is invisible, and the summary said "Missing Backends: None"
    # throughout. Adding a backend to dsdlc means adding it here.
    "obj": [r"^llvmdsdl-uavcan-obj-determinism$"],
}


def _extract_test_names_from_integration_cmake(integration_cmake_text: str) -> Set[str]:
    name_pattern = re.compile(r"\bNAME\s+([A-Za-z0-9_.+-]+)")
    return set(name_pattern.findall(integration_cmake_text))


def _extract_test_names_from_ctest(test_dir: Path, config: str | None) -> Set[str]:
    cmd = ["ctest", "--test-dir", str(test_dir), "-N"]
    if config:
        cmd.extend(["-C", config])
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        stderr = proc.stderr.strip()
        stdout = proc.stdout.strip()
        detail = stderr if stderr else stdout
        raise RuntimeError(f"failed to list tests via ctest: {detail}")

    out: Set[str] = set()
    test_pattern = re.compile(r"^\s*Test\s+#\d+:\s+(.+?)\s*$")
    for line in proc.stdout.splitlines():
        match = test_pattern.match(line)
        if match:
            out.add(match.group(1))
    return out


def _matches(test_names: Iterable[str], patterns: Iterable[str]) -> List[str]:
    out: List[str] = []
    compiled = [re.compile(pattern) for pattern in patterns]
    for name in sorted(set(test_names)):
        if any(pattern.search(name) for pattern in compiled):
            out.append(name)
    return out


def _build_report(
    repo_root: Path,
    integration_cmake_path: Path,
    ctest_test_dir: Path | None,
    ctest_config: str | None,
    junit: "ctest_results.TestResults | None" = None,
) -> Dict[str, object]:
    failed_names: Set[str] = set()
    skipped_names: Set[str] = set()
    if junit is not None:
        test_names = junit.passed
        failed_names = junit.failed
        skipped_names = junit.skipped
        test_source = (
            f"ctest --output-junit (executed: {len(junit.passed)} passed, "
            f"{len(junit.failed)} failed, {len(junit.skipped)} skipped)"
        )
        behavioral = True
    elif ctest_test_dir is not None:
        test_names = _extract_test_names_from_ctest(ctest_test_dir, ctest_config)
        try:
            test_dir_text = str(ctest_test_dir.relative_to(repo_root))
        except ValueError:
            test_dir_text = str(ctest_test_dir)
        test_source = f"ctest --test-dir {test_dir_text}"
        behavioral = False
    else:
        test_names = _extract_test_names_from_integration_cmake(integration_cmake_path.read_text(encoding="utf-8"))
        test_source = str(integration_cmake_path.relative_to(repo_root))
        behavioral = False

    backends: Dict[str, object] = {}
    missing_backends: List[str] = []
    coverage_manifest: List[Dict[str, object]] = []
    covered_backends = 0
    total_backends = 0

    for backend in sorted(MATRIX):
        total_backends += 1
        patterns = MATRIX[backend]
        evidence = _matches(test_names, patterns)
        failed_evidence = _matches(failed_names, patterns) if behavioral else []
        skipped_evidence = _matches(skipped_names, patterns) if behavioral else []
        # Behavioral rule: covered iff a matching test passed AND none failed.
        covered = len(evidence) > 0 and len(failed_evidence) == 0
        if covered:
            covered_backends += 1
        else:
            missing_backends.append(backend)

        backends[backend] = {
            "covered": covered,
            "patterns": list(patterns),
            "evidence_tests": evidence,
            "evidence_failed": failed_evidence,
            "evidence_skipped": skipped_evidence,
        }

        if behavioral and (not covered or skipped_evidence):
            if failed_evidence:
                reason = "matching test(s) failed"
            elif not evidence and skipped_evidence:
                reason = "matching test(s) skipped / not run"
            elif not evidence:
                reason = "no matching test executed"
            else:
                reason = "covered, but some matching tests were skipped"
            coverage_manifest.append(
                {
                    "backend": backend,
                    "covered": covered,
                    "reason": reason,
                    "failed": failed_evidence,
                    "skipped": skipped_evidence,
                }
            )

    return {
        "version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "integration_cmake": str(integration_cmake_path.relative_to(repo_root)),
        "test_source": test_source,
        "test_name_count": len(test_names),
        "behavioral": behavioral,
        "backends": backends,
        "overall_covered_backends": covered_backends,
        "overall_total_backends": total_backends,
        "overall_score": int(round((covered_backends * 100.0) / total_backends)),
        "missing_backends": missing_backends,
        "coverage_manifest": coverage_manifest,
    }


def _write_json(path: Path, data: Dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _write_markdown(path: Path, report: Dict[str, object]) -> None:
    lines: List[str] = []
    lines.append("# Determinism Matrix Coverage")
    lines.append("")
    lines.append(
        "Which axes of generated output are required to be byte-reproducible — across repeated "
        "runs, hosts, and toolchains — and which of those requirements are covered by tests."
    )
    lines.append("")
    lines.append("Generated by `tools/convergence/determinism_matrix_report.py`.")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append("| Backend | Status | Evidence |")
    lines.append("| --- | --- | --- |")
    for backend in sorted(report["backends"]):
        row = report["backends"][backend]
        status = "covered" if row["covered"] else "missing"
        evidence = ", ".join(f"`{name}`" for name in row["evidence_tests"]) if row["evidence_tests"] else "-"
        lines.append(f"| `{backend}` | `{status}` | {evidence} |")
    lines.append("")
    lines.append(f"Overall matrix score: `{report['overall_score']}`")
    lines.append(f"Integration test names scanned: `{report['test_name_count']}`")
    lines.append(f"Test-source scan: `{report['test_source']}`")
    if report.get("behavioral"):
        lines.append(
            "Gating mode: **behavioral** — a backend is `covered` only if its determinism "
            "test executed and passed; a failed/skipped/absent test leaves it uncovered."
        )
    else:
        lines.append(
            "Gating mode: **structural** — coverage is inferred from registered test names. "
            "Supply `--ctest-junit` for behavioral (executed pass/fail) gating."
        )
    lines.append("")

    if report.get("behavioral") and report.get("coverage_manifest"):
        lines.append("## Coverage Manifest (behavioral)")
        lines.append("")
        lines.append("| Backend | Covered | Reason | Failed | Skipped |")
        lines.append("| --- | :---: | --- | --- | --- |")
        for entry in report["coverage_manifest"]:
            failed = ", ".join(f"`{n}`" for n in entry.get("failed", [])) or "-"
            skipped = ", ".join(f"`{n}`" for n in entry.get("skipped", [])) or "-"
            lines.append(
                f"| `{entry['backend']}` | `{entry['covered']}` | {entry['reason']} | {failed} | {skipped} |"
            )
        lines.append("")

    if report["missing_backends"]:
        lines.append("## Missing Backends")
        lines.append("")
        for backend in report["missing_backends"]:
            lines.append(f"- `{backend}`")
        lines.append("")
    else:
        lines.append("## Missing Backends")
        lines.append("")
        lines.append("None.")
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _check_baseline(report: Dict[str, object], baseline_path: Path) -> List[str]:
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    failures: List[str] = []
    report_backends = report["backends"]

    expected_covered = baseline.get("expected_covered", {})
    for backend_name, expected_value in expected_covered.items():
        if backend_name not in report_backends:
            failures.append(f"baseline backend missing from report: {backend_name}")
            continue
        actual_covered = bool(report_backends[backend_name]["covered"])
        if bool(expected_value) != actual_covered:
            failures.append(
                f"coverage drift: backend '{backend_name}' expected {bool(expected_value)} but found {actual_covered}"
            )

    minimum_scores = baseline.get("minimum_scores", {})
    for backend_name, minimum_score in minimum_scores.get("backends", {}).items():
        if backend_name not in report_backends:
            failures.append(f"minimum score references unknown backend: {backend_name}")
            continue
        actual_score = 100 if report_backends[backend_name]["covered"] else 0
        if actual_score < int(minimum_score):
            failures.append(
                f"score regression: backend '{backend_name}' score {actual_score} is below minimum {minimum_score}"
            )

    minimum_overall = minimum_scores.get("overall")
    if minimum_overall is not None:
        actual_overall = int(report["overall_score"])
        if actual_overall < int(minimum_overall):
            failures.append(f"score regression: overall score {actual_overall} is below minimum {minimum_overall}")

    return failures


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate determinism-matrix coverage report for llvm-dsdl backends.")
    parser.add_argument("--repo-root", required=True, help="Repository root.")
    parser.add_argument(
        "--integration-cmake",
        default="test/integration/CMakeLists.txt",
        help="Integration CMakeLists path (relative to repo root unless absolute).",
    )
    parser.add_argument("--output-json", help="Output JSON path.")
    parser.add_argument("--output-md", help="Output Markdown path.")
    parser.add_argument("--ctest-test-dir", help="Configured CTest build directory used for dynamic test-name extraction.")
    parser.add_argument("--ctest-config", help="CTest configuration name (for multi-config generators).")
    parser.add_argument(
        "--ctest-junit",
        action="append",
        default=[],
        help="JUnit XML result file from an executed ctest run; enables behavioral "
        "(executed pass/fail) gating. Repeatable.",
    )
    parser.add_argument("--baseline", help="Path to baseline JSON used for strict regression checks.")
    parser.add_argument(
        "--check-regressions",
        action="store_true",
        help="Fail if determinism matrix coverage regresses.",
    )
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    repo_root = Path(args.repo_root).resolve()
    integration_cmake = Path(args.integration_cmake)
    if not integration_cmake.is_absolute():
        integration_cmake = repo_root / integration_cmake
    if not integration_cmake.exists():
        print(f"error: integration cmake not found: {integration_cmake}", file=sys.stderr)
        return 2

    ctest_test_dir = Path(args.ctest_test_dir).resolve() if args.ctest_test_dir else None

    junit = None
    if args.ctest_junit:
        try:
            junit = ctest_results.load_junit_results([Path(p) for p in args.ctest_junit])
        except (FileNotFoundError, ValueError) as err:
            print(f"error: {err}", file=sys.stderr)
            return 2

    if args.check_regressions and junit is None:
        print(
            "warning: determinism gate running in STRUCTURAL mode (test-name presence); "
            "supply --ctest-junit for behavioral gating",
            file=sys.stderr,
        )

    try:
        report = _build_report(repo_root, integration_cmake, ctest_test_dir, args.ctest_config, junit)
    except RuntimeError as err:
        print(f"error: {err}", file=sys.stderr)
        return 2

    if args.output_json:
        _write_json(Path(args.output_json), report)
    if args.output_md:
        _write_markdown(Path(args.output_md), report)

    if args.check_regressions:
        if args.baseline:
            baseline_path = Path(args.baseline).resolve()
            if not baseline_path.exists():
                print(f"error: baseline not found: {baseline_path}", file=sys.stderr)
                return 2
            failures = _check_baseline(report, baseline_path)
            if failures:
                for failure in failures:
                    print(f"determinism matrix regression: {failure}", file=sys.stderr)
                return 1
        elif report["missing_backends"]:
            for backend in report["missing_backends"]:
                print(f"determinism matrix regression: backend '{backend}' has no coverage evidence", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
