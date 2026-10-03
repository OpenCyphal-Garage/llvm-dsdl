#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Hold the generated Python's serialisation to a baseline of instructions executed.

A second is a property of the machine that measured it, and an instruction count is a property of
the code: cachegrind counts every instruction the interpreter executes, so the count does not move
with load, clock speed or thermal state. The generated Python is interpreted, so no count can be
attributed to a generated function; the interpreter is run twice instead, at N and at 2N
iterations of a payload family, and the difference is N iterations with start-up, imports and
teardown cancelled.

A count belongs to an architecture and an interpreter build, so the baseline is keyed by the first
and records the second as `sys.version` states it: the release, the build's date and its compiler.
Off CI, a missing valgrind, a missing baseline or another interpreter build is a skip, since none
of them is a verdict on the generated code. On CI each is a failure: the image pins the interpreter
and carries valgrind, so a gate that skipped there would be no gate.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# What ctest is told to read as a skip, matching the other lanes that cannot run everywhere.
SKIP_EXIT = 77

SPECIALISATIONS = ("portable", "fast")
FAMILIES = ("small", "medium", "large")

# N for each family. The count is exact, so a short run measures as well as a long one, and
# cachegrind runs the interpreter about seventy times slower than native.
ITERATIONS = {"small": 50, "medium": 25, "large": 10}

# Run under cachegrind as `driver.py PYTHONPATH PACKAGE FAMILY ITERATIONS`. The families are the
# timing benchmark's: one small message, a service's two sections, and three 256-byte payloads. An
# iteration constructs its value through the generated class, whose constructor and defaults are
# generated code too, then serialises, deserialises and re-serialises it. The lists the harness
# builds for a value cost the same in every iteration, and are a small share of one.
DRIVER = """\
import importlib
import sys

pythonpath, package, family, iterations = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
sys.path.insert(0, pythonpath)


def module(name):
    return importlib.import_module(f"{package}.uavcan.{name}")


heartbeat = module("node.heartbeat_1_0")
health = module("node.health_1_0")
mode = module("node.mode_1_0")
execute = module("node.execute_command_1_3")
string = module("primitive.string_1_0")
unstructured = module("primitive.unstructured_1_0")
natural32 = module("primitive.array.natural32_1_0")

Heartbeat = heartbeat.Heartbeat_1_0
Request = execute.ExecuteCommand_1_3Request
Response = execute.ExecuteCommand_1_3Response
String = string.String_1_0
Unstructured = unstructured.Unstructured_1_0
Natural32 = natural32.Natural32_1_0

CASES = {
    "small": [
        (Heartbeat, lambda i: Heartbeat(
            uptime=i & 0xFFFFFFFF,
            health=health.Health_1_0(value=0),
            mode=mode.Mode_1_0(value=0),
            vendor_specific_status_code=i & 0xFF,
        )),
    ],
    "medium": [
        (Request, lambda i: Request(command=65529, parameter=[(i + j) & 0xFF for j in range(64)])),
        (Response, lambda i: Response(status=0, output=[(255 - i - j) & 0xFF for j in range(46)])),
    ],
    "large": [
        (String, lambda i: String(value=[(i + j) & 0xFF for j in range(256)])),
        (Unstructured, lambda i: Unstructured(value=[(i * 3 + j) & 0xFF for j in range(256)])),
        (Natural32, lambda i: Natural32(value=[(i + j) & 0xFFFFFFFF for j in range(64)])),
    ],
}

for cls, make in CASES[family]:
    for i in range(iterations):
        payload = make(i).serialize()
        if cls.deserialize(payload).serialize() != payload:
            raise SystemExit(f"{cls.__name__} did not round-trip")
"""


def on_ci() -> bool:
    """Whether this runs where the image pins the interpreter and carries valgrind."""
    return os.environ.get("GITHUB_ACTIONS") == "true"


def decline(reason: str) -> int:
    """Skip off CI and fail on it, for a reason that is not a verdict on the generated code."""
    if on_ci():
        print(f"error: {reason}", file=sys.stderr)
        return 1
    print(f"skipped: {reason}")
    return SKIP_EXIT


def generate(dsdlc: Path, uavcan_root: Path, out_dir: Path) -> None:
    """Generates the regulated corpus once for each runtime specialisation."""
    for specialisation in SPECIALISATIONS:
        subprocess.run(
            [
                str(dsdlc),
                "--experimental-languages",
                "--target-language",
                "python",
                "--versioned-type-names",
                str(uavcan_root),
                "--outdir",
                str(out_dir / specialisation),
                "--py-package",
                f"llvmdsdl_py_instructions_{specialisation}",
                "--py-runtime-specialization",
                specialisation,
            ],
            check=True,
            capture_output=True,
        )


def environment() -> dict[str, str]:
    """The driver's environment."""
    variables = dict(os.environ)
    # Hash randomisation changes the work a dict does from one run to the next, and a bytecode cache
    # written by the first run would be read by the second; either would leave start-up uncancelled.
    variables.update({"PYTHONHASHSEED": "0", "PYTHONDONTWRITEBYTECODE": "1", "LLVMDSDL_PY_RUNTIME_MODE": "pure"})
    return variables


def write_driver(out_dir: Path) -> Path:
    """Writes the driver beside the packages it imports."""
    driver = out_dir / "driver.py"
    driver.write_text(DRIVER, encoding="utf-8")
    return driver


def smoke(python: str, out_dir: Path) -> None:
    """Runs one iteration of every family natively."""
    driver = write_driver(out_dir)
    for specialisation in SPECIALISATIONS:
        for family in FAMILIES:
            subprocess.run(
                [
                    python,
                    str(driver),
                    str(out_dir / specialisation),
                    f"llvmdsdl_py_instructions_{specialisation}",
                    family,
                    "1",
                ],
                check=True,
                env=environment(),
            )


def instructions(valgrind: str, python: str, driver: Path, arguments: list[str], scratch: Path) -> int:
    """The instructions the interpreter executes running @p driver, start-up included."""
    profile = scratch / "cachegrind.out"
    run = subprocess.run(
        [
            valgrind,
            "--tool=cachegrind",
            "--cache-sim=no",
            "--branch-sim=no",
            f"--cachegrind-out-file={profile}",
            python,
            str(driver),
            *arguments,
        ],
        capture_output=True,
        text=True,
        env=environment(),
    )
    if run.returncode != 0:
        raise RuntimeError(f"cachegrind failed running {driver} {' '.join(arguments)}:\n{run.stderr}")
    for line in profile.read_text(encoding="utf-8").splitlines():
        if line.startswith("summary:"):
            return int(line.split()[1])
    raise RuntimeError(f"{profile} holds no summary line")


def measure(valgrind: str, python: str, out_dir: Path) -> dict[str, dict[str, int]]:
    """The instructions one iteration of each family executes, for each specialisation."""
    driver = write_driver(out_dir)
    counts: dict[str, dict[str, int]] = {}
    with tempfile.TemporaryDirectory(dir=out_dir) as scratch:
        for specialisation in SPECIALISATIONS:
            counts[specialisation] = {}
            package = f"llvmdsdl_py_instructions_{specialisation}"
            for family in FAMILIES:
                n = ITERATIONS[family]
                arguments = [str(out_dir / specialisation), package, family]
                once = instructions(valgrind, python, driver, [*arguments, str(n)], Path(scratch))
                twice = instructions(valgrind, python, driver, [*arguments, str(2 * n)], Path(scratch))
                counts[specialisation][family] = round((twice - once) / n)
    return counts


def compare(observed: dict[str, dict[str, int]], expected: dict, budget_percent: float) -> list[str]:
    """Each count that exceeds its baseline by more than the budget."""
    failures = []
    for specialisation in SPECIALISATIONS:
        for family in FAMILIES:
            count = observed[specialisation][family]
            baseline = int(expected[specialisation][family])
            change = 100.0 * (count - baseline) / baseline
            print(
                f"  {specialisation:8} {family:6} {count:>12,} per iteration, baseline {baseline:>12,} ({change:+.3f}%)"
            )
            if change > budget_percent:
                failures.append(
                    f"{specialisation}/{family}: {count:,} instructions per iteration, {change:+.3f}% over {baseline:,}"
                )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Count the instructions the generated Python executes per serialisation round trip, under "
        "cachegrind, and hold each count to the baseline for this architecture."
    )
    parser.add_argument("--dsdlc", type=Path, help="the compiler; required unless --skip-generate")
    parser.add_argument("--uavcan-root", type=Path, help="the regulated corpus's uavcan namespace")
    parser.add_argument("--out-dir", type=Path, required=True, help="where the packages are generated")
    parser.add_argument("--baseline", type=Path, required=True, help="the baseline file, keyed by architecture")
    parser.add_argument("--python", default=sys.executable, help="the interpreter measured (default: this one)")
    parser.add_argument(
        "--skip-generate", action="store_true", help="measure the packages already in --out-dir instead of generating"
    )
    parser.add_argument(
        "--update", action="store_true", help="write the counts measured as this architecture's baseline"
    )
    arguments = parser.parse_args()
    if not arguments.skip_generate and (arguments.dsdlc is None or arguments.uavcan_root is None):
        parser.error("--dsdlc and --uavcan-root are required unless --skip-generate is given")

    if not arguments.skip_generate:
        # A module an earlier run generated and this one does not would otherwise still import.
        shutil.rmtree(arguments.out_dir, ignore_errors=True)
    arguments.out_dir.mkdir(parents=True, exist_ok=True)
    if not arguments.skip_generate:
        generate(arguments.dsdlc, arguments.uavcan_root, arguments.out_dir)
    # One iteration of every family, natively, so a harness that cannot run fails here rather than as
    # a cachegrind error, and fails where valgrind is absent too.
    smoke(arguments.python, arguments.out_dir)

    valgrind = shutil.which("valgrind")
    if valgrind is None:
        return decline("valgrind is not installed")

    # Two builds of one release execute different instructions, so the release alone does not
    # identify what was measured.
    interpreter = subprocess.run(
        [arguments.python, "-c", "import sys; print(' '.join(sys.version.split()))"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    architecture = platform.machine()
    observed = measure(valgrind, arguments.python, arguments.out_dir)
    entry = {"python": interpreter, **observed}
    print(f"Python {interpreter} on {architecture}, instructions per iteration:")

    baseline = json.loads(arguments.baseline.read_text(encoding="utf-8"))
    if arguments.update:
        baseline["counts"][architecture] = entry
        arguments.baseline.write_text(json.dumps(baseline, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(entry, indent=2))
        print(f"wrote the {architecture} baseline to {arguments.baseline}")
        return 0

    expected = baseline["counts"].get(architecture)
    paste = f'"{architecture}": ' + json.dumps(entry, indent=2)
    if expected is None:
        print(paste, flush=True)
        return decline(
            f"{arguments.baseline} holds no baseline for {architecture}; the counts above are the entry to add"
        )
    if expected["python"] != interpreter:
        print(paste, flush=True)
        return decline(
            f"the {architecture} baseline was taken with Python {expected['python']}, and this is Python "
            f"{interpreter}; re-baseline in the change that moves the interpreter"
        )

    failures = compare(observed, expected, float(baseline["meta"]["budget_percent"]))
    if failures:
        print("The generated Python executes more instructions than its baseline:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        print(
            "The count is deterministic, so this is a change in the generated code or its runtime. Where it is "
            "intended, re-baseline with --update in the change that causes it.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
