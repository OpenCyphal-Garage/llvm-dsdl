#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Validate no-shim hard-cut architecture integrity."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


SURFACE_ROOTS: Sequence[str] = (
    "include/llvmdsdl/CodeGen",
    "lib/CodeGen",
    "include/llvmdsdl/Transforms",
    "lib/Transforms",
)
SURFACE_SUFFIXES = {".h", ".cpp", ".td"}

# Phrases signalling re-introduction of compatibility shim machinery in
# codegen/planning surfaces.
FORBIDDEN_PHRASES: Sequence[Tuple[str, str]] = (
    ("compatibility shim/layer/wrapper", r"\bcompatibility\s+(?:shim|layer|wrapper|alias)\b"),
    ("migration shim/layer/wrapper", r"\bmigration\s+(?:shim|layer|wrapper|alias)\b"),
    ("dual-path architecture marker", r"\bdual[- ]path\b"),
    ("backward-compatibility marker", r"\bbackward[- ]compat(?:ibility)?\b"),
    ("temporary compatibility marker", r"\btemporary\s+(?:compat(?:ibility)?|shim|dual[- ]path)\b"),
)

REQUIRED_CANONICAL_MARKERS: Dict[str, Sequence[str]] = {
    "lib/CodeGen/BodyTranslator.cpp": ("translateFunction(",),
    "test/integration/CMakeLists.txt": (
        "llvmdsdl-backend-contract-",
        "llvmdsdl-parity-matrix-coverage",
        "llvmdsdl-malformed-contract-matrix",
        "llvmdsdl-determinism-matrix-coverage",
    ),
}

def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate no-shim hard-cut architecture integrity.")
    parser.add_argument("--repo-root", required=True, help="Path to repository root.")
    return parser.parse_args(list(argv))


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _line_for_offset(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def _iter_surface_files(repo_root: Path) -> Iterable[Tuple[str, Path]]:
    for rel_root in SURFACE_ROOTS:
        root = repo_root / rel_root
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix in SURFACE_SUFFIXES:
                yield str(path.relative_to(repo_root)), path


def _validate_required_markers(repo_root: Path) -> List[str]:
    failures: List[str] = []
    for rel_path, markers in REQUIRED_CANONICAL_MARKERS.items():
        path = repo_root / rel_path
        if not path.exists():
            failures.append(f"required canonical path missing: {rel_path}")
            continue
        text = _read_text(path)
        for marker in markers:
            if marker not in text:
                failures.append(f"required canonical marker missing: {rel_path} :: {marker}")
    return failures


def _validate_forbidden_phrases(repo_root: Path) -> List[str]:
    failures: List[str] = []
    compiled = [(label, re.compile(pattern, flags=re.IGNORECASE | re.MULTILINE)) for label, pattern in FORBIDDEN_PHRASES]
    for rel_path, abs_path in _iter_surface_files(repo_root):
        text = _read_text(abs_path)
        for label, regex in compiled:
            match = regex.search(text)
            if not match:
                continue
            line = _line_for_offset(text, match.start())
            snippet = match.group(0)
            failures.append(
                f"forbidden hard-cut phrase detected ({label}) in {rel_path}:{line}: '{snippet}'"
            )
    return failures


def main(argv: Iterable[str]) -> int:
    args = parse_args(argv)
    repo_root = Path(args.repo_root).resolve()

    failures: List[str] = []
    failures.extend(_validate_required_markers(repo_root))
    failures.extend(_validate_forbidden_phrases(repo_root))

    if failures:
        for failure in failures:
            print(f"hard-cut regression: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
