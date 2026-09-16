#!/usr/bin/env python3
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
"""
Render the runtime benchmark reports as Markdown.

The two benchmarks write JSON and nothing else, which is the right shape for a
machine and the wrong one for the reason CI runs them. A number nobody reads
cannot become a threshold, and until it is a threshold the lane is only proving
that the harness still compiles.

So this exists to make the numbers legible in the job summary, and to print them
in the shape the threshold files want -- `max_elapsed_sec`, keyed the same way --
so that calibrating a runner is reading a table rather than writing a script.

Ratios are reported separately and deliberately. `fastVsPortableRatio` compares
two specialisations measured in the same process on the same machine, so it is
invariant to how fast that machine happens to be. An absolute second is not: it
encodes the host's CPU, its standard library, and in the Python case the exact
interpreter build. So the absolute thresholds in this repository are calibrated on
one developer machine and enforced nowhere.

The C comparison against Nunavut is reported in instructions, which is a property
of the code rather than of the runner, and its ratios can therefore be read
directly.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

# The families both benchmarks partition their work by, smallest first. Named
# rather than discovered so a family silently vanishing from a report shows up as
# a blank cell instead of a shorter table.
_FAMILIES = ("small", "medium", "large")


def _load(path: pathlib.Path) -> dict | None:
    """Reads a report, or returns None when it was not produced."""
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"warning: cannot read {path}: {exc}", file=sys.stderr)
        return None


def _rust_rows(report: dict) -> list[tuple[str, str, str, dict]]:
    """Flattens the Rust report to (mode, family, operation, metrics)."""
    rows = []
    for mode, mode_report in sorted(report.get("modes", {}).items()):
        families = mode_report.get("families", {})
        for family in _FAMILIES:
            if family not in families:
                continue
            for operation in ("encode", "decode"):
                metrics = families[family].get(operation)
                if metrics:
                    rows.append((mode, family, operation, metrics))
    return rows


def _python_rows(report: dict) -> list[tuple[str, str, str, dict]]:
    """Flattens the Python report to (specialisation, runtime mode, family, metrics).

    A mode whose backend is unavailable carries an `error` rather than timings --
    the accelerator is optional and `accel` is absent wherever it was not built --
    so those are skipped rather than reported as zero.
    """
    rows = []
    for spec, spec_report in sorted(report.get("specializations", {}).items()):
        for mode, mode_report in sorted(spec_report.get("modes", {}).items()):
            if mode_report.get("status") != "ok":
                continue
            families = mode_report.get("families", {})
            for family in _FAMILIES:
                if family in families:
                    rows.append((spec, mode, family, families[family]))
    return rows


def _render_rust(report: dict | None, out: list[str]) -> None:
    out.append("## Rust runtime benchmark")
    if report is None:
        out.append("")
        out.append("_No report produced -- the benchmark did not run._")
        return
    out.append("")
    iterations = report.get("iterations", {})
    out.append(
        "Iterations: "
        + ", ".join(f"{f}={iterations.get(f, '?')}" for f in _FAMILIES)
        + f" · inline threshold {report.get('inline_threshold_bytes', '?')} bytes"
    )
    out.append("")
    out.append("| memory mode | family | op | elapsed (s) | ops/sec | MiB/s |")
    out.append("|---|---|---|---:|---:|---:|")
    for mode, family, operation, m in _rust_rows(report):
        out.append(
            f"| {mode} | {family} | {operation} | {m.get('elapsed_sec', 0):.6f} | "
            f"{m.get('operations_per_sec', 0):,.0f} | {m.get('throughput_mib_per_sec', 0):,.1f} |"
        )


def _render_python(report: dict | None, out: list[str]) -> None:
    out.append("## Python runtime benchmark")
    if report is None:
        out.append("")
        out.append("_No report produced -- the benchmark did not run._")
        return
    out.append("")
    out.append(f"Interpreter: `{report.get('pythonVersion', '?').splitlines()[0]}`")
    out.append("")
    # CPU seconds beside wall seconds. They agree on an idle machine and diverge by exactly the
    # amount the process spent waiting for a CPU, which makes the pair readable as "was this
    # measurement taken on a busy box" without needing to know anything about the runner.
    out.append("| specialisation | runtime mode | family | wall (s) | CPU (s) | CPU/wall | ops/sec |")
    out.append("|---|---|---|---:|---:|---:|---:|")
    for spec, mode, family, m in _python_rows(report):
        wall = m.get("elapsedSec", 0.0)
        cpu = m.get("cpuSec")
        cpu_cell = f"{cpu:.6f}" if cpu is not None else "—"
        ratio_cell = f"{cpu / wall:.3f}" if cpu is not None and wall > 0 else "—"
        out.append(
            f"| {spec} | {mode} | {family} | {wall:.6f} | {cpu_cell} | {ratio_cell} | "
            f"{m.get('operationsPerSec', 0):,.0f} |"
        )

    # The scale-invariant half. A ratio between two specialisations measured in
    # one process says something about the generated code; an absolute second
    # says something about the runner.
    comparisons = report.get("comparisons", {})
    ratios = [
        (name, mode, family, value)
        for name, by_mode in sorted(comparisons.items())
        for mode, by_family in sorted(by_mode.items())
        for family, value in sorted(by_family.items())
        if value is not None
    ]
    if ratios:
        out.append("")
        out.append("### Ratios (host-independent)")
        out.append("")
        out.append("| comparison | runtime mode | family | ratio |")
        out.append("|---|---|---|---:|")
        for name, mode, family, value in ratios:
            out.append(f"| {name} | {mode} | {family} | {value:.4f} |")


def _serdes_net_ir(operation: dict, iterations: int) -> dict[str, float] | None:
    """The per-iteration cost of each implementation, with the loop subtracted.

    Each figure is the difference between the same measurement taken at the full
    iteration count and at zero, which is what removes process start, the
    harness's fixture search and its printing. `noop` is the loop and the
    indirect call with no serialiser behind it.
    """
    if iterations <= 0:
        return None

    def per_iteration(key: str) -> float | None:
        entry = operation.get(key)
        if not entry:
            return None
        return (entry["irAtFull"] - entry["irAtZero"]) / iterations

    noop = per_iteration("noop")
    if noop is None:
        return None
    net = {}
    for key in ("c", "obj", "nnvg"):
        value = per_iteration(key)
        if value is None:
            return None
        net[key] = value - noop
    return net


def _render_serdes(reports: list[tuple[pathlib.Path, dict | None]], out: list[str]) -> None:
    out.append("## Generated C against Nunavut, in instructions")
    if not reports:
        return
    out.append("")
    out.append(
        "Three implementations of the same ten types: `nnvg` is the peer's C, `c` is this "
        "project's C compiled the same way, and `obj` is the object dsdlc emits itself through "
        "its own LLVM pipeline at O2. All three decode the same wire image and are measured "
        "behind the same indirect call, so the ratios compare bodies. This project emits its "
        "serialisers out of line and Nunavut emits `static inline` definitions, so at a real call "
        "site the peer's body is visible to the optimiser where ours is not; that difference is "
        "inside these numbers."
    )

    for path, report in reports:
        extra = (report or {}).get("meta", {}).get("dsdlcExtraArguments", "")
        title = f"`{extra}`" if extra else "default flags"
        out.append("")
        out.append(f"### dsdlc: {title}")
        out.append("")
        if report is None:
            out.append(f"_No report at `{path}`; the comparison skips where valgrind is absent._")
            continue

        meta = report.get("meta", {})
        iterations = int(meta.get("iterations", 0))
        out.append(
            f"{meta.get('arch', '?')} · {iterations} iterations · "
            f"`{meta.get('cCompiler', '?')}` `{meta.get('cFlags', '?')}` · "
            f"{meta.get('valgrind', '?')}"
        )
        out.append("")
        out.append(
            "| type | wire bytes | fixture | op | nnvg (Ir) | c (Ir) | c/nnvg | obj (Ir) "
            "| obj/nnvg | obj/c |"
        )
        out.append("|---|---:|---|---|---:|---:|---:|---:|---:|---:|")
        ratios: dict[str, dict[str, list[float]]] = {}
        for case in report.get("cases", []):
            for operation in ("encode", "decode"):
                net = _serdes_net_ir(case.get(operation, {}), iterations)
                if net is None:
                    continue
                pairs = {
                    "c/nnvg": net["c"] / net["nnvg"],
                    "obj/nnvg": net["obj"] / net["nnvg"],
                    "obj/c": net["obj"] / net["c"],
                }
                for name, value in pairs.items():
                    ratios.setdefault(operation, {}).setdefault(name, []).append(value)
                out.append(
                    f"| {case.get('name', '?')} | {case.get('wireBytes', '?')} | "
                    f"{case.get('fixtureSource', '?')} | {operation} | {net['nnvg']:,.1f} | "
                    f"{net['c']:,.1f} | {pairs['c/nnvg']:.3f} | {net['obj']:,.1f} | "
                    f"{pairs['obj/nnvg']:.3f} | {pairs['obj/c']:.3f} |"
                )

        if ratios:
            out.append("")
            out.append("| op | c/nnvg | obj/nnvg | obj/c |")
            out.append("|---|---:|---:|---:|")
            for operation, by_name in ratios.items():
                cells = []
                for name in ("c/nnvg", "obj/nnvg", "obj/c"):
                    values = by_name[name]
                    product = 1.0
                    for value in values:
                        product *= value
                    cells.append(f"{product ** (1.0 / len(values)):.3f}")
                out.append(f"| {operation} | " + " | ".join(cells) + " |")
            out.append("")
            out.append("Geometric means of the per-type ratios.")


def _render_calibration(rust: dict | None, python: dict | None, out: list[str]) -> None:
    """Prints the observed elapsed times as threshold-file fragments.

    Calibrating a runner means running this lane a handful of times and taking
    the per-cell maximum plus a budget. Emitting the shape here means that is a
    copy rather than a transcription, which is where the current files' drift
    came from.
    """
    out.append("")
    out.append("<details><summary>Observed elapsed seconds, in <code>max_elapsed_sec</code> shape</summary>")
    out.append("")
    out.append("```json")
    fragment: dict = {}
    if rust is not None:
        fragment["rust"] = {
            mode: {
                family: {
                    op: round(m.get("elapsed_sec", 0.0), 6)
                    for _, f, op, m in _rust_rows(rust)
                    if (_, f) == (mode, family)
                }
                for family in _FAMILIES
            }
            for mode in sorted(rust.get("modes", {}))
        }
    if python is not None:
        py: dict = {}
        for spec, mode, family, m in _python_rows(python):
            entry = py.setdefault(spec, {}).setdefault(mode, {})
            entry[family] = round(m.get("elapsedSec", 0.0), 6)
            if m.get("cpuSec") is not None:
                py.setdefault(spec + " (cpu)", {}).setdefault(mode, {})[family] = round(m["cpuSec"], 6)
        fragment["python"] = py
    out.append(json.dumps(fragment, indent=2, sort_keys=True))
    out.append("```")
    out.append("")
    out.append("</details>")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rust-report", type=pathlib.Path, required=True)
    parser.add_argument("--python-report", type=pathlib.Path, required=True)
    parser.add_argument(
        "--serdes-report",
        type=pathlib.Path,
        action="append",
        default=[],
        help="A C serdes instruction comparison report; repeatable, one per dsdlc variant.",
    )
    parser.add_argument(
        "--markdown",
        type=pathlib.Path,
        help="Append the rendered report here (GITHUB_STEP_SUMMARY); stdout when omitted.",
    )
    args = parser.parse_args(argv)

    rust = _load(args.rust_report)
    python = _load(args.python_report)

    out: list[str] = ["# Runtime benchmarks", ""]
    out.append(
        "Reported, not enforced. The checked-in thresholds are calibrated on one "
        "developer machine (macOS/arm64) and do not describe this runner; see "
        "`.github/workflows/ci.yml` for what turning them on requires."
    )
    out.append("")
    out.append(
        "Regressions in the generated Rust *are* gated, by "
        "`llvmdsdl-fixtures-rust-runtime-instructions` in the main suite, which counts instructions "
        "under cachegrind instead of seconds and so needs no calibration. Nothing on this page "
        "fails a build."
    )
    out.append("")
    _render_rust(rust, out)
    out.append("")
    _render_python(python, out)
    out.append("")
    _render_serdes([(path, _load(path)) for path in args.serdes_report], out)
    _render_calibration(rust, python, out)
    out.append("")

    text = "\n".join(out)
    if args.markdown is not None:
        with args.markdown.open("a", encoding="utf-8") as handle:
            handle.write(text)
    else:
        sys.stdout.write(text)

    # A report that was asked for and not produced means the benchmark did not
    # run, which the lane must not pass over quietly.
    missing = [
        str(p)
        for p, r in ((args.rust_report, rust), (args.python_report, python))
        if r is None
    ]
    if missing:
        print("error: benchmark report(s) missing: " + ", ".join(missing), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
