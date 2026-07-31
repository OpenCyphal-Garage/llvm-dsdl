#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Run build-integration cells: probe the toolchain, stage a copy, build, execute, report.

Each cell under cells/ is one (language, build system) pair. A cell owns nothing but build wiring --
its round-trip program comes from src/ and its schema from dsdl/, both shared by every cell in the
row -- so the difference between two cells is exactly the thing the documentation is about.

Three properties of this runner are deliberate:

Cells are STAGED, not run in place. Every run copies the cell directory together with dsdl/ and src/
into a scratch directory and runs there. That keeps the source tree clean, but the real reason is
that it continuously proves the property the documentation promises: a reader who copies one cell
directory plus dsdl/ and src/ has everything, because that is literally what CI runs. A cell that
reached back into this repository would fail here rather than in a user's checkout.

Steps are DECLARED, not scripted. cell.json lists the commands verbatim, and the documentation
generator renders that same list onto the cell's page. A recipe in the manual cannot drift from the
recipe CI runs because there is only one of them.

A missing toolchain SKIPS, loudly. Thirteen cells span six ecosystems and no single machine has all
of them; a cell that cannot run must say so rather than fail. But "everything skipped" must never
read as success, which is what --require is for: the core cells named there turn a skip into a
failure, so an image or runner that quietly lost a toolchain is caught instead of being congratulated.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

EXAMPLE_ROOT = Path(__file__).resolve().parent
CELLS_ROOT = EXAMPLE_ROOT / "cells"
DSDL_ROOT = EXAMPLE_ROOT / "dsdl"
SRC_ROOT = EXAMPLE_ROOT / "src"

# The cell invariant: build wiring only. A cell directory that carried its own round-trip program
# would make two cells in the same row differ by more than their build files, which is precisely the
# comparison the showroom exists to support. The allowlist below is for files that are genuinely
# build wiring despite their extension -- a Cargo build script is Rust, but it is not the program
# under test.
SOURCE_SUFFIXES = frozenset(
    {".c", ".h", ".cpp", ".cxx", ".cc", ".hpp", ".hxx", ".rs", ".go", ".ts", ".mts", ".py"}
)
BUILD_WIRING_ALLOWLIST = frozenset(
    {"build.rs", "setup.py", "conftest.py", "noxfile.py", "hatch_build.py"}
)

IDIOMS = frozenset({"A", "B"})
DEP_STRATEGIES = frozenset({"list-outputs", "depfile", "stamp", "none"})

REQUIRED_KEYS = frozenset(
    {"name", "title", "row", "language", "build_system", "idiom", "dep_strategy", "summary",
     "requires", "steps"}
)
OPTIONAL_KEYS = frozenset({"notes", "generated_dir"})

PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"

# Returned when every cell that was asked for skipped. CTest is told to read this as a skip via
# SKIP_RETURN_CODE, so a runner without (say) pnpm reports "skipped" rather than a green pass that
# claims coverage it does not have. 77 is the autotools convention for the same idea.
EXIT_ALL_SKIPPED = 77


class CellError(Exception):
    """A cell is malformed. Distinct from a cell that runs and fails."""


@dataclasses.dataclass(frozen=True)
class Requirement:
    command: str
    reason: str


@dataclasses.dataclass(frozen=True)
class Step:
    name: str
    run: tuple[str, ...]
    # Extra environment for this step, with the same {dsdlc}/{python}/{prefix} substitutions applied
    # to the values. Some build systems take the path to a code generator through the environment
    # rather than the command line -- `//go:generate $DSDLC ...` is the motivating case -- and
    # forcing those into argv would misrepresent how the tool is actually driven.
    env: tuple[tuple[str, str], ...] = ()


@dataclasses.dataclass(frozen=True)
class Cell:
    name: str
    title: str
    row: str
    language: str
    build_system: str
    idiom: str
    dep_strategy: str
    summary: str
    requires: tuple[Requirement, ...]
    steps: tuple[Step, ...]
    notes: str
    generated_dir: str
    directory: Path


@dataclasses.dataclass
class Result:
    cell: str
    status: str
    detail: str
    seconds: float
    failed_step: str | None = None


# --------------------------------------------------------------------------------------------------
# Loading and validation.
#
# Strict on purpose. cell.json is read by two consumers -- this runner and the documentation
# generator -- and a typo that silently drops a field would produce a page that quietly misdescribes
# what CI ran. An unknown key is a mistake worth stopping for.

def load_cell(directory: Path) -> Cell:
    manifest = directory / "cell.json"
    try:
        raw = json.loads(manifest.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise CellError(f"{manifest}: invalid JSON: {exc}") from exc

    if not isinstance(raw, dict):
        raise CellError(f"{manifest}: top level must be an object")

    keys = set(raw)
    missing = REQUIRED_KEYS - keys
    if missing:
        raise CellError(f"{manifest}: missing required key(s): {', '.join(sorted(missing))}")
    unknown = keys - REQUIRED_KEYS - OPTIONAL_KEYS
    if unknown:
        raise CellError(f"{manifest}: unknown key(s): {', '.join(sorted(unknown))}")

    if raw["name"] != directory.name:
        raise CellError(
            f"{manifest}: name '{raw['name']}' does not match directory '{directory.name}'; "
            "the directory name is what --require and the docs URL use"
        )
    if raw["idiom"] not in IDIOMS:
        raise CellError(f"{manifest}: idiom must be one of {sorted(IDIOMS)}, got '{raw['idiom']}'")
    if raw["dep_strategy"] not in DEP_STRATEGIES:
        raise CellError(
            f"{manifest}: dep_strategy must be one of {sorted(DEP_STRATEGIES)}, "
            f"got '{raw['dep_strategy']}'"
        )

    requires = []
    for entry in raw["requires"]:
        if not isinstance(entry, dict) or set(entry) != {"command", "reason"}:
            raise CellError(f"{manifest}: each 'requires' entry needs exactly 'command' and 'reason'")
        requires.append(Requirement(entry["command"], entry["reason"]))

    steps = []
    for entry in raw["steps"]:
        if not isinstance(entry, dict) or not {"name", "run"} <= set(entry) \
                or set(entry) - {"name", "run", "env"}:
            raise CellError(
                f"{manifest}: each 'steps' entry needs 'name' and 'run', and may add 'env'")
        if not entry["run"] or not all(isinstance(a, str) for a in entry["run"]):
            raise CellError(f"{manifest}: step '{entry['name']}' needs a non-empty argv of strings")
        env = entry.get("env", {})
        if not isinstance(env, dict) or not all(
                isinstance(k, str) and isinstance(v, str) for k, v in env.items()):
            raise CellError(f"{manifest}: step '{entry['name']}' env must map strings to strings")
        steps.append(Step(entry["name"], tuple(entry["run"]), tuple(sorted(env.items()))))
    if not steps:
        raise CellError(f"{manifest}: a cell with no steps cannot demonstrate anything")

    return Cell(
        name=raw["name"],
        title=raw["title"],
        row=raw["row"],
        language=raw["language"],
        build_system=raw["build_system"],
        idiom=raw["idiom"],
        dep_strategy=raw["dep_strategy"],
        summary=raw["summary"],
        requires=tuple(requires),
        steps=tuple(steps),
        notes=raw.get("notes", ""),
        generated_dir=raw.get("generated_dir", "generated"),
        directory=directory,
    )


def discover_cells(cells_root: Path = CELLS_ROOT) -> list[Cell]:
    if not cells_root.is_dir():
        return []
    cells = [
        load_cell(child)
        for child in sorted(cells_root.iterdir())
        if child.is_dir() and (child / "cell.json").is_file()
    ]
    return cells


def check_invariant(cell: Cell) -> list[str]:
    """Return violations of the build-wiring-only rule for one cell directory."""
    violations = []
    for path in sorted(cell.directory.rglob("*")):
        if not path.is_file():
            continue
        if path.name in BUILD_WIRING_ALLOWLIST:
            continue
        if path.suffix in SOURCE_SUFFIXES:
            rel = path.relative_to(cell.directory)
            violations.append(
                f"{cell.name}/{rel}: cells carry build wiring only; shared round-trip sources "
                f"live in src/ (add to BUILD_WIRING_ALLOWLIST if this really is build wiring)"
            )
    return violations


# --------------------------------------------------------------------------------------------------
# Execution.

def missing_requirements(cell: Cell) -> list[Requirement]:
    return [req for req in cell.requires if shutil.which(req.command) is None]


def stage(cell: Cell, work_root: Path) -> Path:
    """Copy the cell plus the shared schema and sources into a scratch directory."""
    staged = work_root / cell.name
    if staged.exists():
        shutil.rmtree(staged)
    staged.mkdir(parents=True)

    shutil.copytree(cell.directory, staged, dirs_exist_ok=True)
    if DSDL_ROOT.is_dir():
        shutil.copytree(DSDL_ROOT, staged / "dsdl", dirs_exist_ok=True)
    if SRC_ROOT.is_dir():
        shutil.copytree(SRC_ROOT, staged / "src", dirs_exist_ok=True)
    return staged


def needs_prefix(cell: Cell) -> bool:
    """True when any step refers to {prefix}, i.e. the cell consumes an installed llvm-dsdl."""
    return any("{prefix}" in arg for step in cell.steps for arg in step.run)


def run_cell(cell: Cell, dsdlc: Path, work_root: Path, verbose: bool,
             prefix: Path | None = None) -> Result:
    started = time.monotonic()

    absent = missing_requirements(cell)
    if absent:
        detail = "; ".join(f"{r.command} ({r.reason})" for r in absent)
        return Result(cell.name, SKIP, f"missing: {detail}", 0.0)

    # A cell that calls find_package(llvm-dsdl) needs an install tree, not just the binary. Skipping
    # is right rather than falling back to the raw binary: the point of such a cell is to exercise
    # the installed package, and a fallback would let a broken package config pass unnoticed.
    if needs_prefix(cell) and prefix is None:
        return Result(cell.name, SKIP, "needs an installed llvm-dsdl (--prefix)", 0.0)

    staged = stage(cell, work_root)
    substitutions = {
        "dsdlc": str(dsdlc),
        "python": sys.executable,
        "prefix": str(prefix) if prefix else "",
    }

    for step in cell.steps:
        argv = [arg.format_map(substitutions) for arg in step.run]
        env = None
        if step.env:
            env = dict(os.environ)
            env.update({k: v.format_map(substitutions) for k, v in step.env})
        if verbose:
            shown = " ".join(f"{k}={v.format_map(substitutions)}" for k, v in step.env)
            prefix_text = f"{shown} " if shown else ""
            print(f"  [{cell.name}] {step.name}: {prefix_text}{' '.join(argv)}", flush=True)
        completed = subprocess.run(
            argv,
            cwd=staged,
            env=env,
            capture_output=not verbose,
            text=True,
        )
        if completed.returncode != 0:
            if not verbose:
                sys.stderr.write(completed.stdout or "")
                sys.stderr.write(completed.stderr or "")
            return Result(
                cell.name,
                FAIL,
                f"step '{step.name}' exited {completed.returncode}",
                time.monotonic() - started,
                failed_step=step.name,
            )

    return Result(cell.name, PASS, "", time.monotonic() - started)


# --------------------------------------------------------------------------------------------------
# Reporting.

def print_matrix(cells: list[Cell], results: list[Result]) -> None:
    print()
    print("Build integration matrix")
    print("=" * 78)
    if not cells:
        print("  no cells registered")
        print("=" * 78)
        return

    by_name = {r.cell: r for r in results}
    width = max(len(c.name) for c in cells)
    current_row = None
    for cell in cells:
        if cell.row != current_row:
            current_row = cell.row
            print(f"\n{cell.row}")
        result = by_name.get(cell.name)
        if result is None:
            print(f"  {cell.name:<{width}}  ----  not run")
            continue
        timing = f"{result.seconds:6.1f}s" if result.status != SKIP else "      "
        suffix = f"  {result.detail}" if result.detail else ""
        print(f"  {cell.name:<{width}}  {result.status}  {timing}{suffix}")
    print("=" * 78)
    tally = {status: sum(1 for r in results if r.status == status) for status in (PASS, FAIL, SKIP)}
    print(f"  {tally[PASS]} passed, {tally[FAIL]} failed, {tally[SKIP]} skipped")


def render_markdown(cells: list[Cell], results: list[Result]) -> str:
    """A matrix for a CI job summary.

    Rendered here rather than by the workflow so that the summary and the console report cannot
    disagree, and so the workflow stays free of the inline scripting that block scalars make awkward
    to quote correctly.
    """
    lines = ["## Build integration matrix", ""]
    if not cells:
        lines += ["No cells are registered yet.", ""]
        return "\n".join(lines)

    by_name = {r.cell: r for r in results}
    lines += ["| Cell | Language | Build system | Status | Detail |", "|---|---|---|---|---|"]
    for cell in cells:
        result = by_name.get(cell.name)
        status = result.status if result else "NOT RUN"
        detail = result.detail if result and result.detail else ""
        lines.append(
            f"| `{cell.name}` | {cell.row} | {cell.build_system} | {status} | {detail} |"
        )
    tally = {s: sum(1 for r in results if r.status == s) for s in (PASS, FAIL, SKIP)}
    lines += ["", f"{tally[PASS]} passed, {tally[FAIL]} failed, {tally[SKIP]} skipped", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("cells", nargs="*", help="cell names to run (default: all)")
    parser.add_argument("--dsdlc", type=Path, help="path to the dsdlc binary under test")
    parser.add_argument("--work", type=Path, default=EXAMPLE_ROOT / ".work",
                        help="scratch directory for staged cells (default: ./.work)")
    parser.add_argument("--prefix", type=Path,
                        help="install prefix of llvm-dsdl, for cells that use find_package")
    parser.add_argument("--require", default="",
                        help="comma-separated cells whose skip is a failure")
    parser.add_argument("--list", action="store_true", help="list registered cells and exit")
    parser.add_argument("--check-invariant", action="store_true",
                        help="check that cells carry build wiring only, and exit")
    parser.add_argument("--json", type=Path, help="write machine-readable results here")
    parser.add_argument("--markdown", type=Path,
                        help="append a matrix table here (for a CI job summary)")
    parser.add_argument("--verbose", "-v", action="store_true", help="stream step output")
    args = parser.parse_args()

    try:
        cells = discover_cells()
    except CellError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.cells:
        known = {c.name for c in cells}
        unknown = [n for n in args.cells if n not in known]
        if unknown:
            print(f"error: unknown cell(s): {', '.join(unknown)}", file=sys.stderr)
            print(f"known: {', '.join(sorted(known)) or '(none)'}", file=sys.stderr)
            return 2
        cells = [c for c in cells if c.name in set(args.cells)]

    if args.list:
        if not cells:
            print("no cells registered")
            return 0
        for cell in cells:
            print(f"{cell.name}\t{cell.row}\t{cell.build_system}\tidiom {cell.idiom}\t"
                  f"{cell.dep_strategy}")
        return 0

    if args.check_invariant:
        violations = [v for cell in cells for v in check_invariant(cell)]
        for violation in violations:
            print(f"error: {violation}", file=sys.stderr)
        print(f"checked {len(cells)} cell(s): {len(violations)} violation(s)")
        return 1 if violations else 0

    # Nothing below runs a cell without a compiler to run it with, but --list and --check-invariant
    # above are useful on a machine that has never built the project, so the requirement lands here.
    if cells and args.dsdlc is None:
        print("error: --dsdlc is required to run cells", file=sys.stderr)
        return 2
    dsdlc = args.dsdlc.resolve() if args.dsdlc else Path()
    if cells and not dsdlc.is_file():
        print(f"error: no dsdlc at {dsdlc}", file=sys.stderr)
        return 2

    required = {name for name in args.require.split(",") if name}
    unknown_required = required - {c.name for c in cells}
    if unknown_required and not args.cells:
        print(f"error: --require names unknown cell(s): {', '.join(sorted(unknown_required))}",
              file=sys.stderr)
        return 2

    prefix = args.prefix.resolve() if args.prefix else None
    if prefix is not None and not prefix.is_dir():
        print(f"error: --prefix {prefix} is not a directory", file=sys.stderr)
        return 2

    args.work.mkdir(parents=True, exist_ok=True)
    results = [run_cell(cell, dsdlc, args.work, args.verbose, prefix) for cell in cells]

    print_matrix(cells, results)

    forced = [r for r in results if r.status == SKIP and r.cell in required]
    for result in forced:
        print(f"error: {result.cell} is required but skipped -- {result.detail}", file=sys.stderr)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps([dataclasses.asdict(r) for r in results], indent=2) + "\n",
            encoding="utf-8",
        )

    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        with args.markdown.open("a", encoding="utf-8") as handle:
            handle.write(render_markdown(cells, results))

    if any(r.status == FAIL for r in results) or forced:
        return 1
    if results and all(r.status == SKIP for r in results):
        return EXIT_ALL_SKIPPED
    return 0


if __name__ == "__main__":
    sys.exit(main())
