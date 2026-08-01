#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Run the showroom's build recipes: probe the toolchain, stage a copy, build, execute, report.

Each recipe under recipes/ is one (language, build system) pair. A recipe owns nothing but build wiring --
its round-trip program comes from src/ and its schema from dsdl/, both shared by every recipe in the
row -- so the difference between two recipes is exactly the thing the documentation is about.

Three properties of this runner are deliberate:

Recipes are STAGED, not run in place. Every run copies the recipe directory together with dsdl/ and src/
into a scratch directory and runs there. That keeps the source tree clean, but the real reason is
that it continuously proves the property the documentation promises: a reader who copies one recipe
directory plus dsdl/ and src/ has everything, because that is literally what CI runs. A recipe that
reached back into this repository would fail here rather than in a user's checkout.

Steps are DECLARED, not scripted. recipe.json lists the commands verbatim, and the documentation
generator renders that same list onto the recipe's page. A recipe in the manual cannot drift from the
recipe CI runs because there is only one of them.

A missing toolchain SKIPS, loudly. Thirteen recipes span six ecosystems and no single machine has all
of them; a recipe that cannot run must say so rather than fail. But "everything skipped" must never
read as success, which is what --require is for: the core recipes named there turn a skip into a
failure, so an image or runner that quietly lost a toolchain is caught instead of being congratulated.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

EXAMPLE_ROOT = Path(__file__).resolve().parent
RECIPES_ROOT = EXAMPLE_ROOT / "recipes"
DSDL_ROOT = EXAMPLE_ROOT / "dsdl"
SRC_ROOT = EXAMPLE_ROOT / "src"

# The recipe invariant: build wiring only. A recipe directory that carried its own round-trip program
# would make two recipes in the same row differ by more than their build files, which is precisely the
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

# Returned when every recipe that was asked for skipped. CTest is told to read this as a skip via
# SKIP_RETURN_CODE, so a runner without (say) pnpm reports "skipped" rather than a green pass that
# claims coverage it does not have. 77 is the autotools convention for the same idea.
EXIT_ALL_SKIPPED = 77


class RecipeError(Exception):
    """A recipe is malformed. Distinct from a recipe that runs and fails."""


@dataclasses.dataclass(frozen=True)
class Requirement:
    command: str
    reason: str
    # Optional "at least this version", compared against the first dotted number the tool prints for
    # --version. A recipe that needs a build-system feature rather than merely the build system is
    # otherwise indistinguishable from one that does not, and would fail on an old host instead of
    # skipping -- GNU Make's grouped targets (4.3) are the motivating case, since macOS ships 3.81.
    min_version: str = ""


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
class Recipe:
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
    recipe: str
    status: str
    detail: str
    seconds: float
    failed_step: str | None = None


# --------------------------------------------------------------------------------------------------
# Loading and validation.
#
# Strict on purpose. recipe.json is read by two consumers -- this runner and the documentation
# generator -- and a typo that silently drops a field would produce a page that quietly misdescribes
# what CI ran. An unknown key is a mistake worth stopping for.

def load_recipe(directory: Path) -> Recipe:
    manifest = directory / "recipe.json"
    try:
        raw = json.loads(manifest.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise RecipeError(f"{manifest}: invalid JSON: {exc}") from exc

    if not isinstance(raw, dict):
        raise RecipeError(f"{manifest}: top level must be an object")

    keys = set(raw)
    missing = REQUIRED_KEYS - keys
    if missing:
        raise RecipeError(f"{manifest}: missing required key(s): {', '.join(sorted(missing))}")
    unknown = keys - REQUIRED_KEYS - OPTIONAL_KEYS
    if unknown:
        raise RecipeError(f"{manifest}: unknown key(s): {', '.join(sorted(unknown))}")

    if raw["name"] != directory.name:
        raise RecipeError(
            f"{manifest}: name '{raw['name']}' does not match directory '{directory.name}'; "
            "the directory name is what --require and the docs URL use"
        )
    if raw["idiom"] not in IDIOMS:
        raise RecipeError(f"{manifest}: idiom must be one of {sorted(IDIOMS)}, got '{raw['idiom']}'")
    if raw["dep_strategy"] not in DEP_STRATEGIES:
        raise RecipeError(
            f"{manifest}: dep_strategy must be one of {sorted(DEP_STRATEGIES)}, "
            f"got '{raw['dep_strategy']}'"
        )

    requires = []
    for entry in raw["requires"]:
        if not isinstance(entry, dict) or not {"command", "reason"} <= set(entry) \
                or set(entry) - {"command", "reason", "min_version"}:
            raise RecipeError(
                f"{manifest}: each 'requires' entry needs 'command' and 'reason', "
                "and may add 'min_version'")
        requires.append(
            Requirement(entry["command"], entry["reason"], entry.get("min_version", "")))

    steps = []
    for entry in raw["steps"]:
        if not isinstance(entry, dict) or not {"name", "run"} <= set(entry) \
                or set(entry) - {"name", "run", "env"}:
            raise RecipeError(
                f"{manifest}: each 'steps' entry needs 'name' and 'run', and may add 'env'")
        if not entry["run"] or not all(isinstance(a, str) for a in entry["run"]):
            raise RecipeError(f"{manifest}: step '{entry['name']}' needs a non-empty argv of strings")
        env = entry.get("env", {})
        if not isinstance(env, dict) or not all(
                isinstance(k, str) and isinstance(v, str) for k, v in env.items()):
            raise RecipeError(f"{manifest}: step '{entry['name']}' env must map strings to strings")
        steps.append(Step(entry["name"], tuple(entry["run"]), tuple(sorted(env.items()))))
    if not steps:
        raise RecipeError(f"{manifest}: a recipe with no steps cannot demonstrate anything")

    return Recipe(
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


def discover_recipes(recipes_root: Path = RECIPES_ROOT) -> list[Recipe]:
    if not recipes_root.is_dir():
        return []
    recipes = [
        load_recipe(child)
        for child in sorted(recipes_root.iterdir())
        if child.is_dir() and (child / "recipe.json").is_file()
    ]
    return recipes


def check_invariant(recipe: Recipe) -> list[str]:
    """Return violations of the build-wiring-only rule for one recipe directory."""
    violations = []
    for path in sorted(recipe.directory.rglob("*")):
        if not path.is_file():
            continue
        if path.name in BUILD_WIRING_ALLOWLIST:
            continue
        if path.suffix in SOURCE_SUFFIXES:
            rel = path.relative_to(recipe.directory)
            violations.append(
                f"{recipe.name}/{rel}: recipes carry build wiring only; shared round-trip sources "
                f"live in src/ (add to BUILD_WIRING_ALLOWLIST if this really is build wiring)"
            )
    return violations


# --------------------------------------------------------------------------------------------------
# Execution.

def _version_tuple(text: str) -> tuple[int, ...]:
    match = re.search(r"(\d+(?:\.\d+)+)", text)
    return tuple(int(p) for p in match.group(1).split(".")) if match else ()


def unmet_requirements(recipe: Recipe) -> list[str]:
    """Return one human-readable reason per requirement the host does not satisfy."""
    unmet = []
    for req in recipe.requires:
        if shutil.which(req.command) is None:
            unmet.append(f"{req.command} not found ({req.reason})")
            continue
        if not req.min_version:
            continue
        try:
            probe = subprocess.run([req.command, "--version"], capture_output=True, text=True,
                                   timeout=30)
        except (OSError, subprocess.SubprocessError):
            unmet.append(f"{req.command} would not report a version (need >= {req.min_version})")
            continue
        found = _version_tuple(probe.stdout or probe.stderr or "")
        if not found:
            unmet.append(f"{req.command} reported no parseable version (need >= {req.min_version})")
        elif found < _version_tuple(req.min_version):
            shown = ".".join(str(p) for p in found)
            unmet.append(f"{req.command} {shown} < {req.min_version} ({req.reason})")
    return unmet


def stage(recipe: Recipe, work_root: Path) -> Path:
    """Copy the recipe plus the shared schema and sources into a scratch directory."""
    staged = work_root / recipe.name
    if staged.exists():
        shutil.rmtree(staged)
    staged.mkdir(parents=True)

    shutil.copytree(recipe.directory, staged, dirs_exist_ok=True)
    if DSDL_ROOT.is_dir():
        shutil.copytree(DSDL_ROOT, staged / "dsdl", dirs_exist_ok=True)
    if SRC_ROOT.is_dir():
        shutil.copytree(SRC_ROOT, staged / "src", dirs_exist_ok=True)
    return staged


def needs_prefix(recipe: Recipe) -> bool:
    """True when any step refers to {prefix}, i.e. the recipe consumes an installed llvm-dsdl."""
    return any("{prefix}" in arg for step in recipe.steps for arg in step.run)


def run_recipe(recipe: Recipe, dsdlc: Path, work_root: Path, verbose: bool,
             prefix: Path | None = None) -> Result:
    started = time.monotonic()

    unmet = unmet_requirements(recipe)
    if unmet:
        return Result(recipe.name, SKIP, "; ".join(unmet), 0.0)

    # A recipe that calls find_package(llvm-dsdl) needs an install tree, not just the binary. Skipping
    # is right rather than falling back to the raw binary: the point of such a recipe is to exercise
    # the installed package, and a fallback would let a broken package config pass unnoticed.
    if needs_prefix(recipe) and prefix is None:
        return Result(recipe.name, SKIP, "needs an installed llvm-dsdl (--prefix)", 0.0)

    staged = stage(recipe, work_root)
    substitutions = {
        "dsdlc": str(dsdlc),
        "python": sys.executable,
        "prefix": str(prefix) if prefix else "",
    }

    for step in recipe.steps:
        argv = [arg.format_map(substitutions) for arg in step.run]
        env = None
        if step.env:
            env = dict(os.environ)
            env.update({k: v.format_map(substitutions) for k, v in step.env})
        if verbose:
            shown = " ".join(f"{k}={v.format_map(substitutions)}" for k, v in step.env)
            prefix_text = f"{shown} " if shown else ""
            print(f"  [{recipe.name}] {step.name}: {prefix_text}{' '.join(argv)}", flush=True)
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
                recipe.name,
                FAIL,
                f"step '{step.name}' exited {completed.returncode}",
                time.monotonic() - started,
                failed_step=step.name,
            )

    return Result(recipe.name, PASS, "", time.monotonic() - started)


# --------------------------------------------------------------------------------------------------
# Reporting.

def print_matrix(recipes: list[Recipe], results: list[Result]) -> None:
    print()
    print("Build integration matrix")
    print("=" * 78)
    if not recipes:
        print("  no recipes registered")
        print("=" * 78)
        return

    by_name = {r.recipe: r for r in results}
    width = max(len(c.name) for c in recipes)
    current_row = None
    for recipe in recipes:
        if recipe.row != current_row:
            current_row = recipe.row
            print(f"\n{recipe.row}")
        result = by_name.get(recipe.name)
        if result is None:
            print(f"  {recipe.name:<{width}}  ----  not run")
            continue
        timing = f"{result.seconds:6.1f}s" if result.status != SKIP else "      "
        suffix = f"  {result.detail}" if result.detail else ""
        print(f"  {recipe.name:<{width}}  {result.status}  {timing}{suffix}")
    print("=" * 78)
    tally = {status: sum(1 for r in results if r.status == status) for status in (PASS, FAIL, SKIP)}
    print(f"  {tally[PASS]} passed, {tally[FAIL]} failed, {tally[SKIP]} skipped")


def render_markdown(recipes: list[Recipe], results: list[Result]) -> str:
    """A matrix for a CI job summary.

    Rendered here rather than by the workflow so that the summary and the console report cannot
    disagree, and so the workflow stays free of the inline scripting that block scalars make awkward
    to quote correctly.
    """
    lines = ["## Build integration matrix", ""]
    if not recipes:
        lines += ["No recipes are registered yet.", ""]
        return "\n".join(lines)

    by_name = {r.recipe: r for r in results}
    lines += ["| Recipe | Language | Build system | Status | Detail |", "|---|---|---|---|---|"]
    for recipe in recipes:
        result = by_name.get(recipe.name)
        status = result.status if result else "NOT RUN"
        detail = result.detail if result and result.detail else ""
        lines.append(
            f"| `{recipe.name}` | {recipe.row} | {recipe.build_system} | {status} | {detail} |"
        )
    tally = {s: sum(1 for r in results if r.status == s) for s in (PASS, FAIL, SKIP)}
    lines += ["", f"{tally[PASS]} passed, {tally[FAIL]} failed, {tally[SKIP]} skipped", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("recipes", nargs="*", help="recipe names to run (default: all)")
    parser.add_argument("--dsdlc", type=Path, help="path to the dsdlc binary under test")
    parser.add_argument("--work", type=Path, default=EXAMPLE_ROOT / ".work",
                        help="scratch directory for staged recipes (default: ./.work)")
    parser.add_argument("--prefix", type=Path,
                        help="install prefix of llvm-dsdl, for recipes that use find_package")
    parser.add_argument("--require", default="",
                        help="comma-separated recipes whose skip is a failure")
    parser.add_argument("--list", action="store_true", help="list registered recipes and exit")
    parser.add_argument("--check-invariant", action="store_true",
                        help="check that recipes carry build wiring only, and exit")
    parser.add_argument("--json", type=Path, help="write machine-readable results here")
    parser.add_argument("--markdown", type=Path,
                        help="append a matrix table here (for a CI job summary)")
    parser.add_argument("--verbose", "-v", action="store_true", help="stream step output")
    args = parser.parse_args()

    try:
        recipes = discover_recipes()
    except RecipeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.recipes:
        known = {c.name for c in recipes}
        unknown = [n for n in args.recipes if n not in known]
        if unknown:
            print(f"error: unknown recipe(s): {', '.join(unknown)}", file=sys.stderr)
            print(f"known: {', '.join(sorted(known)) or '(none)'}", file=sys.stderr)
            return 2
        recipes = [c for c in recipes if c.name in set(args.recipes)]

    if args.list:
        if not recipes:
            print("no recipes registered")
            return 0
        for recipe in recipes:
            print(f"{recipe.name}\t{recipe.row}\t{recipe.build_system}\tidiom {recipe.idiom}\t"
                  f"{recipe.dep_strategy}")
        return 0

    if args.check_invariant:
        violations = [v for recipe in recipes for v in check_invariant(recipe)]
        for violation in violations:
            print(f"error: {violation}", file=sys.stderr)
        print(f"checked {len(recipes)} recipe(s): {len(violations)} violation(s)")
        return 1 if violations else 0

    # Nothing below runs a recipe without a compiler to run it with, but --list and --check-invariant
    # above are useful on a machine that has never built the project, so the requirement lands here.
    if recipes and args.dsdlc is None:
        print("error: --dsdlc is required to run recipes", file=sys.stderr)
        return 2
    dsdlc = args.dsdlc.resolve() if args.dsdlc else Path()
    if recipes and not dsdlc.is_file():
        print(f"error: no dsdlc at {dsdlc}", file=sys.stderr)
        return 2

    required = {name for name in args.require.split(",") if name}
    unknown_required = required - {c.name for c in recipes}
    if unknown_required and not args.recipes:
        print(f"error: --require names unknown recipe(s): {', '.join(sorted(unknown_required))}",
              file=sys.stderr)
        return 2

    prefix = args.prefix.resolve() if args.prefix else None
    if prefix is not None and not prefix.is_dir():
        print(f"error: --prefix {prefix} is not a directory", file=sys.stderr)
        return 2

    args.work.mkdir(parents=True, exist_ok=True)
    results = [run_recipe(recipe, dsdlc, args.work, args.verbose, prefix) for recipe in recipes]

    print_matrix(recipes, results)

    forced = [r for r in results if r.status == SKIP and r.recipe in required]
    for result in forced:
        print(f"error: {result.recipe} is required but skipped -- {result.detail}", file=sys.stderr)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps([dataclasses.asdict(r) for r in results], indent=2) + "\n",
            encoding="utf-8",
        )

    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        with args.markdown.open("a", encoding="utf-8") as handle:
            handle.write(render_markdown(recipes, results))

    if any(r.status == FAIL for r in results) or forced:
        return 1
    if results and all(r.status == SKIP for r in results):
        return EXIT_ALL_SKIPPED
    return 0


if __name__ == "__main__":
    sys.exit(main())
