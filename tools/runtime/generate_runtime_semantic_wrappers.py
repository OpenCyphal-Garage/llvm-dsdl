#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Generate runtime semantic-wrapper artifacts from in-repo templates."""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List

DEFAULT_REPO_ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class GenerationTarget:
    name: str
    template: Path
    output: Path
    generated_header: str


# These headers ship verbatim inside every generated package, so they carry only
# what a consumer of that package needs. The template a header came from is a
# repository concern; the --check gate keeps the pair in step.
RUST_GENERATED_HEADER = """//===----------------------------------------------------------------------===//
// NOTE: LLVMDSDL AUTO-GENERATED FILE. DO NOT EDIT.
//===----------------------------------------------------------------------===//

"""

PYTHON_GENERATED_HEADER = """#===----------------------------------------------------------------------===#
# NOTE: LLVMDSDL AUTO-GENERATED FILE. DO NOT EDIT.
#===----------------------------------------------------------------------===#

"""


def _normalize(text: str) -> str:
    if not text.endswith("\n"):
        text += "\n"
    return text


def _render_target(target: GenerationTarget) -> str:
    template_text = target.template.read_text(encoding="utf-8")
    return _normalize(target.generated_header + template_text)


def _process_target(target: GenerationTarget, check_only: bool, failures: List[str], updates: List[str]) -> None:
    expected = _render_target(target)
    if target.output.exists():
        actual = target.output.read_text(encoding="utf-8")
    else:
        actual = ""

    if actual == expected:
        return

    if check_only:
        failures.append(
            f"semantic-wrapper generation drift: {target.output} is out of date; run "
            "tools/runtime/generate_runtime_semantic_wrappers.py"
        )
        return

    target.output.parent.mkdir(parents=True, exist_ok=True)
    target.output.write_text(expected, encoding="utf-8")
    updates.append(str(target.output))


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate runtime semantic-wrapper artifacts.")
    parser.add_argument(
        "--repo-root",
        default=str(DEFAULT_REPO_ROOT),
        help=f"Path to repository root (default: {DEFAULT_REPO_ROOT}).",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Validate generated artifacts are up to date without modifying files.",
    )
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    args = parse_args(argv)
    repo_root = Path(args.repo_root).resolve()

    targets = [
        GenerationTarget(
            name="rust-semantic-wrappers",
            template=repo_root / "runtime/rust/dsdl_runtime_semantic_wrappers.rs.in",
            output=repo_root / "runtime/rust/dsdl_runtime_semantic_wrappers.rs",
            generated_header=RUST_GENERATED_HEADER,
        ),
        GenerationTarget(
            name="python-runtime-loader",
            template=repo_root / "runtime/python/_runtime_loader.py.in",
            output=repo_root / "runtime/python/_runtime_loader.py",
            generated_header=PYTHON_GENERATED_HEADER,
        ),
    ]

    missing_templates = [target.template for target in targets if not target.template.exists()]
    if missing_templates:
        for template in missing_templates:
            print(f"semantic-wrapper generation regression: missing template: {template}")
        return 2

    failures: List[str] = []
    updates: List[str] = []
    for target in targets:
        _process_target(target, args.check, failures, updates)

    if failures:
        for failure in failures:
            print(f"semantic-wrapper generation regression: {failure}")
        return 1

    if args.check:
        print("runtime semantic-wrapper generation check passed")
    elif updates:
        for update in updates:
            print(f"updated: {update}")
    else:
        print("runtime semantic-wrapper artifacts already up to date")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
