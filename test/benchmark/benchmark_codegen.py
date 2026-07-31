#!/usr/bin/env python3
#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#

"""Benchmark harness for multi-language dsdlc generation throughput."""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class LanguageSpec:
    """One benchmarked language command variant."""

    key: str
    args: list[str]
    use_lookup_dirs: bool = True


DEFAULT_LANGUAGE_SPECS: list[LanguageSpec] = [
    LanguageSpec("c", ["--target-language", "c"]),
    LanguageSpec("cpp", ["--target-language", "cpp", "--cpp-profile", "both"]),
    LanguageSpec(
        "rust",
        ["--target-language", "rust", "--rust-profile", "std", "--rust-crate-name", "civildrone_bench"],
    ),
    LanguageSpec("go", ["--target-language", "go", "--go-module", "civildrone/bench"]),
    LanguageSpec("ts", ["--target-language", "ts", "--ts-module", "civildrone_bench_ts"]),
    LanguageSpec("python", ["--target-language", "python", "--py-package", "civildrone_bench_py"]),
]

EXPERIMENT_LANGUAGE_SPECS: list[LanguageSpec] = [
    LanguageSpec("embedC", ["--target-language", "c"], use_lookup_dirs=False),
    LanguageSpec("noEmbedC", ["--target-language", "c", "--no-embedded-uavcan"], use_lookup_dirs=True),
]

LANGUAGE_SPECS: list[LanguageSpec] = DEFAULT_LANGUAGE_SPECS + EXPERIMENT_LANGUAGE_SPECS
LANGUAGE_KEY_ALIASES = {
    "embedc": "embedC",
    "noembedc": "noEmbedC",
}


def parse_selected_specs(language_arg: str) -> list[LanguageSpec]:
    if not language_arg or language_arg.strip().lower() == "all":
        return list(DEFAULT_LANGUAGE_SPECS)
    requested = [x.strip() for x in language_arg.split(",") if x.strip()]
    by_key = {spec.key: spec for spec in LANGUAGE_SPECS}
    selected: list[LanguageSpec] = []
    for key in requested:
        resolved_key = LANGUAGE_KEY_ALIASES.get(key.lower(), key)
        if resolved_key not in by_key:
            raise KeyError(f"unknown language key '{key}', expected one of: {', '.join(sorted(by_key))}")
        selected.append(by_key[resolved_key])
    return selected


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def count_files(root: Path) -> int:
    count = 0
    for _dirpath, _dirnames, filenames in os.walk(root):
        count += len(filenames)
    return count


def forward_stream_lines(stream: Any, sink: Any, prefix: str, collector: list[str]) -> None:
    """Continuously forwards process output and collects it for post-run reporting."""
    try:
        for line in iter(stream.readline, ""):
            collector.append(line)
            sink.write(f"{prefix}{line}")
            sink.flush()
    finally:
        stream.close()


def describe_process_metrics(pid: int) -> tuple[str, float | None]:
    """Returns lightweight child-process telemetry and RSS MiB when available."""
    try:
        proc = subprocess.run(
            ["ps", "-o", "%cpu=,rss=", "-p", str(pid)],
            check=False,
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            return f"pid={pid} cpu=n/a rss=n/a", None
        values = proc.stdout.strip().split()
        if len(values) < 2:
            return f"pid={pid} cpu=n/a rss=n/a", None
        cpu = values[0]
        rss_kib = float(values[1])
        rss_mib = rss_kib / 1024.0
        return f"pid={pid} cpu={cpu}% rss={rss_mib:.1f}MiB", rss_mib
    except (OSError, ValueError):
        return f"pid={pid} cpu=n/a rss=n/a", None


def run_language(
    dsdlc: Path,
    root_namespace_dir: Path,
    lookup_dirs: list[Path],
    out_base_dir: Path,
    spec: LanguageSpec,
    iterations: int,
    optimize_lowered_serdes: bool,
    timeout_seconds: int,
    status_interval_sec: int,
    max_rss_mib: float,
) -> dict[str, Any]:
    samples: list[float] = []
    output_dir = out_base_dir / spec.key
    output_dir.mkdir(parents=True, exist_ok=True)
    peak_rss_mib = 0.0

    for idx in range(iterations):
        run_out_dir = output_dir / f"iter-{idx + 1}"
        if run_out_dir.exists():
            shutil.rmtree(run_out_dir)
        run_out_dir.mkdir(parents=True, exist_ok=True)

        command = [str(dsdlc)]
        if optimize_lowered_serdes:
            command.append("--optimize-lowered-serdes")
        command.extend(spec.args)
        if spec.use_lookup_dirs:
            for lookup_dir in lookup_dirs:
                command.extend(["--lookup-dir", str(lookup_dir)])
        command.extend(["--outdir", str(run_out_dir), str(root_namespace_dir)])

        print(
            f"[benchmark] {spec.key} iter {idx + 1}/{iterations}: start",
            flush=True,
        )
        start = time.perf_counter()
        proc = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        if proc.stdout is None or proc.stderr is None:
            raise RuntimeError(f"failed to capture process pipes for {spec.key}")

        stdout_lines: list[str] = []
        stderr_lines: list[str] = []
        stdout_thread = threading.Thread(
            target=forward_stream_lines,
            args=(proc.stdout, sys.stdout, f"[{spec.key} stdout] ", stdout_lines),
            daemon=True,
        )
        stderr_thread = threading.Thread(
            target=forward_stream_lines,
            args=(proc.stderr, sys.stderr, f"[{spec.key} stderr] ", stderr_lines),
            daemon=True,
        )
        stdout_thread.start()
        stderr_thread.start()

        next_status_at = float(status_interval_sec)
        timed_out = False
        last_metrics = f"pid={proc.pid} cpu=n/a rss=n/a"
        initial_metrics, initial_rss_mib = describe_process_metrics(proc.pid)
        last_metrics = initial_metrics
        if initial_rss_mib is not None:
            peak_rss_mib = max(peak_rss_mib, initial_rss_mib)
        while True:
            ret = proc.poll()
            elapsed = time.perf_counter() - start
            if ret is not None:
                break
            if timeout_seconds > 0 and elapsed >= float(timeout_seconds):
                timed_out = True
                proc.terminate()
                try:
                    proc.wait(timeout=5.0)
                except subprocess.TimeoutExpired:
                    proc.kill()
                break
            metrics, rss_mib = describe_process_metrics(proc.pid)
            last_metrics = metrics
            if rss_mib is not None:
                peak_rss_mib = max(peak_rss_mib, rss_mib)
            if status_interval_sec > 0 and elapsed >= next_status_at:
                try:
                    file_count = count_files(run_out_dir)
                except OSError:
                    file_count = -1
                stdout_line_count = len(stdout_lines)
                stderr_line_count = len(stderr_lines)
                quiet_hint = ""
                if file_count == 0 and stdout_line_count == 0 and stderr_line_count == 0:
                    quiet_hint = " phase=frontend/lowering(no emitted files yet)"
                if file_count >= 0:
                    print(
                            f"[benchmark] {spec.key} iter {idx + 1}/{iterations}: running "
                            f"{elapsed:.1f}s (generated files so far: {file_count}, "
                            f"streamed lines stdout={stdout_line_count} stderr={stderr_line_count}, "
                            f"{last_metrics}){quiet_hint}",
                            flush=True,
                        )
                else:
                    print(
                        f"[benchmark] {spec.key} iter {idx + 1}/{iterations}: running "
                        f"{elapsed:.1f}s (streamed lines stdout={stdout_line_count} "
                        f"stderr={stderr_line_count}, {last_metrics}){quiet_hint}",
                        flush=True,
                    )
                next_status_at += float(status_interval_sec)
            time.sleep(0.25)

        proc.wait()
        stdout_thread.join(timeout=2.0)
        stderr_thread.join(timeout=2.0)
        stdout = "".join(stdout_lines)
        stderr = "".join(stderr_lines)
        elapsed = time.perf_counter() - start
        if timed_out:
            raise RuntimeError(
                f"benchmark generation timed out for {spec.key} after {elapsed:.1f}s "
                f"(timeout={timeout_seconds}s)"
            )
        if proc.returncode != 0:
            sys.stderr.write(f"[{spec.key}] command failed ({proc.returncode}): {' '.join(command)}\n")
            if stdout:
                sys.stderr.write(f"[{spec.key}] stdout:\n{stdout}\n")
            if stderr:
                sys.stderr.write(f"[{spec.key}] stderr:\n{stderr}\n")
            raise RuntimeError(f"benchmark generation failed for {spec.key}")
        samples.append(elapsed)
        _, rss_mib = describe_process_metrics(proc.pid)
        if rss_mib is not None:
            peak_rss_mib = max(peak_rss_mib, rss_mib)
        if max_rss_mib > 0 and peak_rss_mib > max_rss_mib:
            raise RuntimeError(
                f"benchmark generation exceeded max RSS for {spec.key}: "
                f"peak={peak_rss_mib:.1f}MiB limit={max_rss_mib:.1f}MiB"
            )
        print(
            f"[benchmark] {spec.key} iter {idx + 1}/{iterations}: {elapsed:.3f}s",
            flush=True,
        )

    median_s = statistics.median(samples)
    result = {
        "command_args": spec.args,
        "iterations": iterations,
        "samples_sec": samples,
        "median_sec": median_s,
        "min_sec": min(samples),
        "max_sec": max(samples),
        "generated_file_count": count_files(output_dir),
        "output_dir": str(output_dir),
        "peak_rss_mib": peak_rss_mib,
    }
    return result


def run_suite(args: argparse.Namespace) -> dict[str, Any]:
    dsdlc = Path(args.dsdlc).resolve()
    root_namespace_dir = Path(args.root_namespace_dir).resolve()
    lookup_dirs = [Path(value).resolve() for value in args.lookup_dir]
    out_base_dir = Path(args.out_base_dir).resolve()
    selected_specs = parse_selected_specs(args.languages)
    out_base_dir.mkdir(parents=True, exist_ok=True)

    if not dsdlc.exists():
        raise FileNotFoundError(f"dsdlc not found: {dsdlc}")
    if not root_namespace_dir.exists():
        raise FileNotFoundError(f"root namespace dir not found: {root_namespace_dir}")
    wrapper_has_civildrone = (root_namespace_dir / "civildrone").is_dir()
    wrapper_has_uavcan = (root_namespace_dir / "uavcan").exists()
    if wrapper_has_civildrone and wrapper_has_uavcan:
        raise ValueError(
            "root namespace dir points at a wrapper directory containing multiple namespaces. "
            "Use --root-namespace-dir <...>/civildrone and --lookup-dir <...>/uavcan."
        )
    if root_namespace_dir.name == "civildrone" and not lookup_dirs and any(spec.use_lookup_dirs for spec in selected_specs):
        raise ValueError(
            "benchmark root points to civildrone without lookup roots; pass --lookup-dir "
            "submodules/public_regulated_data_types/uavcan, "
            "or run only no-lookup lanes like embedC."
        )
    for lookup_dir in lookup_dirs:
        if not lookup_dir.exists():
            raise FileNotFoundError(f"lookup dir not found: {lookup_dir}")

    language_results: dict[str, dict[str, Any]] = {}
    suite_start = time.perf_counter()
    for spec in selected_specs:
        language_results[spec.key] = run_language(
            dsdlc=dsdlc,
            root_namespace_dir=root_namespace_dir,
            lookup_dirs=lookup_dirs,
            out_base_dir=out_base_dir,
            spec=spec,
            iterations=args.iterations,
            optimize_lowered_serdes=args.optimize_lowered_serdes,
            timeout_seconds=args.per_language_timeout_sec,
            status_interval_sec=args.status_interval_sec,
            max_rss_mib=args.max_rss_mib,
        )
    total_elapsed = time.perf_counter() - suite_start

    report = {
        "schema_version": 1,
        "suite_name": "llvmdsdl-codegen-complex",
        "created_utc": utc_now(),
        "host": {
            "platform": platform.platform(),
            "python": sys.version,
            "cpu_count": os.cpu_count(),
        },
        "config": {
            "dsdlc": str(dsdlc),
            "root_namespace_dir": str(root_namespace_dir),
            "lookup_dirs": [str(path) for path in lookup_dirs],
            "out_base_dir": str(out_base_dir),
            "iterations": args.iterations,
            "optimize_lowered_serdes": args.optimize_lowered_serdes,
            "per_language_timeout_sec": args.per_language_timeout_sec,
            "max_rss_mib": args.max_rss_mib,
            "languages": [spec.key for spec in selected_specs],
        },
        "languages": language_results,
        "total_elapsed_sec": total_elapsed,
    }
    return report


def print_report_summary(report: dict[str, Any], title: str) -> None:
    print(title)
    print(f"created: {report.get('created_utc', 'n/a')}")
    print(f"dataset: {report['config']['root_namespace_dir']}")
    print(f"iterations: {report['config']['iterations']}")
    print("")
    print(f"{'language':<10} {'median(s)':>10} {'min(s)':>10} {'max(s)':>10} {'files':>10} {'peakRSS(MiB)':>13}")
    print("-" * 72)
    report_keys = set(report["languages"].keys())
    ordered_keys = [spec.key for spec in LANGUAGE_SPECS if spec.key in report_keys]
    for key in ordered_keys:
        lang = report["languages"][key]
        print(
            f"{key:<10} {lang['median_sec']:>10.3f} {lang['min_sec']:>10.3f} "
            f"{lang['max_sec']:>10.3f} {lang['generated_file_count']:>10} "
            f"{lang.get('peak_rss_mib', 0.0):>13.1f}"
        )
    print("-" * 72)
    print(f"{'total':<10} {report['total_elapsed_sec']:>10.3f}")


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def describe_source_report(path: Path) -> str:
    """Where a threshold file came from, said in a way that means the same thing on another machine.

    Provenance, so it wants to be readable rather than resolvable: the recorded report lives in a
    build tree that no one else has. Recording the absolute path put one developer's home directory
    in a committed file, which is both noise in every diff and wrong for every other reader.
    """
    repo_root = Path(__file__).resolve().parents[2]
    try:
        return path.resolve().relative_to(repo_root).as_posix()
    except ValueError:
        return str(path.resolve())  # outside the repo: nothing relative to say


def init_thresholds_from_report(args: argparse.Namespace) -> dict[str, Any]:
    report = read_json(Path(args.report_json).resolve())
    baselines: dict[str, float] = {}
    for key, data in report["languages"].items():
        baselines[key] = float(data["median_sec"])

    threshold_data = {
        "schema_version": 1,
        "suite_name": "llvmdsdl-codegen-complex-thresholds",
        "created_utc": utc_now(),
        "source_report": describe_source_report(Path(args.report_json)),
        "baselines_sec": baselines,
        "profiles": {
            "dev_ab": {
                "description": "Developer A/B guard for same hardware or similar workstation loads.",
                "relative_multiplier": args.dev_relative_multiplier,
                "absolute_margin_sec": args.dev_absolute_margin_sec,
            },
            "ci_oom": {
                "description": "CI guard to catch order-of-magnitude regressions across variable hosts.",
                "relative_multiplier": args.ci_relative_multiplier,
                "absolute_margin_sec": args.ci_absolute_margin_sec,
            },
        },
        "profile_overrides": {},
    }
    return threshold_data


def check_against_thresholds(
    report: dict[str, Any], thresholds: dict[str, Any], profile_name: str
) -> tuple[list[str], list[str], dict[str, Any]]:
    baselines = thresholds.get("baselines_sec", {})
    profiles = thresholds.get("profiles", {})
    overrides = thresholds.get("profile_overrides", {})
    if profile_name not in profiles:
        raise KeyError(f"profile '{profile_name}' not found in thresholds")

    profile = profiles[profile_name]
    default_mult = float(profile["relative_multiplier"])
    default_margin = float(profile["absolute_margin_sec"])
    profile_override = overrides.get(profile_name, {})

    failures: list[str] = []
    passes: list[str] = []
    details: dict[str, Any] = {}
    for key in report["languages"].keys():
        if key not in baselines:
            failures.append(f"{key}: missing baseline")
            continue
        baseline = float(baselines[key])
        measured = float(report["languages"][key]["median_sec"])
        override = profile_override.get(key, {})
        mult = float(override.get("relative_multiplier", default_mult))
        margin = float(override.get("absolute_margin_sec", default_margin))
        max_allowed = (baseline * mult) + margin
        ok = measured <= max_allowed
        summary = (
            f"{key}: measured={measured:.3f}s baseline={baseline:.3f}s "
            f"limit={max_allowed:.3f}s (x{mult:.3f} + {margin:.3f}s)"
        )
        details[key] = {
            "baseline_sec": baseline,
            "measured_sec": measured,
            "relative_multiplier": mult,
            "absolute_margin_sec": margin,
            "max_allowed_sec": max_allowed,
            "pass": ok,
        }
        if ok:
            passes.append(summary)
        else:
            failures.append(summary)

    return passes, failures, details


def validate_thresholds_ready(thresholds: dict[str, Any], selected_languages: list[str]) -> None:
    baselines = thresholds.get("baselines_sec", {})
    missing = [key for key in selected_languages if key not in baselines]
    if missing:
        raise KeyError(f"threshold baselines missing keys: {', '.join(missing)}")

    non_positive = [key for key in selected_languages if float(baselines[key]) <= 0.0]
    if non_positive:
        raise ValueError(
            "threshold baselines are uninitialized/non-positive for: " + ", ".join(non_positive)
        )

    src = str(thresholds.get("source_report", ""))
    if src == "uninitialized":
        raise ValueError(
            "threshold file is a template (source_report=uninitialized). "
            "Run record + init-thresholds to calibrate first."
        )


def cmd_record(args: argparse.Namespace) -> int:
    report = run_suite(args)
    write_json(Path(args.report_json).resolve(), report)
    print_report_summary(report, "llvmdsdl codegen benchmark (record)")
    print(f"report-json: {Path(args.report_json).resolve()}")
    return 0


def cmd_init_thresholds(args: argparse.Namespace) -> int:
    threshold_data = init_thresholds_from_report(args)
    out_path = Path(args.thresholds_json).resolve()
    write_json(out_path, threshold_data)
    print(f"thresholds-json: {out_path}")
    return 0


def cmd_check(args: argparse.Namespace) -> int:
    thresholds = read_json(Path(args.thresholds_json).resolve())
    selected_specs = parse_selected_specs(args.languages)
    selected_keys = [spec.key for spec in selected_specs]
    validate_thresholds_ready(thresholds, selected_keys)

    report = run_suite(args)
    passes, failures, details = check_against_thresholds(report, thresholds, args.profile)

    check_report = {
        "schema_version": 1,
        "created_utc": utc_now(),
        "profile": args.profile,
        "passes": passes,
        "failures": failures,
        "details": details,
        "benchmark_report": report,
    }
    write_json(Path(args.report_json).resolve(), check_report)

    print_report_summary(report, f"llvmdsdl codegen benchmark (check:{args.profile})")
    print("")
    for line in passes:
        print(f"PASS: {line}")
    for line in failures:
        print(f"FAIL: {line}")
    print(f"check-report-json: {Path(args.report_json).resolve()}")

    if failures:
        return 1
    return 0


def add_common_runtime_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--dsdlc", required=True, help="Path to dsdlc executable.")
    parser.add_argument("--root-namespace-dir", required=True, help="DSDL root namespace directory.")
    parser.add_argument(
        "--lookup-dir",
        action="append",
        default=[],
        help="Optional additional lookup root. Repeat to add more dependency roots.",
    )
    parser.add_argument("--out-base-dir", required=True, help="Output directory root for generated files.")
    parser.add_argument("--report-json", required=True, help="Output report JSON path.")
    parser.add_argument("--iterations", type=int, default=1, help="Iterations per language.")
    parser.add_argument(
        "--languages",
        default="all",
        help=(
            "Comma-separated language keys to benchmark. "
            "default=all (c,cpp,rust,go,ts,python). "
            "Optional experiment keys: embedC (embedded-uavcan C lane), "
            "noEmbedC (C lane with --no-embedded-uavcan)."
        ),
    )
    parser.add_argument(
        "--optimize-lowered-serdes",
        action="store_true",
        help="Pass --optimize-lowered-serdes to dsdlc for each benchmark command.",
    )
    parser.add_argument(
        "--per-language-timeout-sec",
        type=int,
        default=0,
        help="Per-language timeout in seconds (0 disables timeout).",
    )
    parser.add_argument(
        "--status-interval-sec",
        type=int,
        default=15,
        help="Heartbeat status print interval while a language command is running (0 disables).",
    )
    parser.add_argument(
        "--max-rss-mib",
        type=float,
        default=0.0,
        help="Maximum allowed per-language peak RSS in MiB (0 disables memory cap).",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Benchmark llvmdsdl generation throughput on a large corpus.")
    sub = parser.add_subparsers(dest="command", required=True)

    p_record = sub.add_parser("record", help="Run benchmark and write timing report JSON.")
    add_common_runtime_args(p_record)
    p_record.set_defaults(func=cmd_record)

    p_check = sub.add_parser("check", help="Run benchmark and check against threshold profile.")
    add_common_runtime_args(p_check)
    p_check.add_argument("--thresholds-json", required=True, help="Threshold configuration JSON.")
    p_check.add_argument("--profile", default="ci_oom", help="Threshold profile name.")
    p_check.set_defaults(func=cmd_check)

    p_init = sub.add_parser(
        "init-thresholds",
        help="Create a threshold JSON from a benchmark report with default profile margins.",
    )
    p_init.add_argument("--report-json", required=True, help="Input benchmark report JSON (from record mode).")
    p_init.add_argument("--thresholds-json", required=True, help="Output threshold JSON.")
    p_init.add_argument("--dev-relative-multiplier", type=float, default=1.35)
    p_init.add_argument("--dev-absolute-margin-sec", type=float, default=2.0)
    p_init.add_argument("--ci-relative-multiplier", type=float, default=10.0)
    p_init.add_argument("--ci-absolute-margin-sec", type=float, default=30.0)
    p_init.set_defaults(func=cmd_init_thresholds)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args))
    except subprocess.TimeoutExpired as ex:
        sys.stderr.write(f"benchmark command timeout: {ex}\n")
        return 2
    except Exception as ex:  # pylint: disable=broad-except
        sys.stderr.write(f"benchmark harness error: {ex}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
