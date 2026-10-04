#!/usr/bin/env python3
#===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===//

"""Compare generated snapshot text against a golden that carries a version placeholder.

The goldens under golden/ts_python_snapshots are dsdlc output with the generator's version in the
banner replaced by <LLVMDSDL_VERSION>. Bumping VERSION therefore moves no golden. The generated side
gets the same substitution here; the golden is compared as stored, so a golden refreshed by
copying dsdlc output over it fails on the version line instead of quietly reinstating a literal.

Refresh the goldens from a fresh run:

  LLVMDSDL_UPDATE_CODEGEN_SNAPSHOT=1 <lit> <build>/test/lit/<config>

A golden compared with --pending is a target written by hand: the output dsdlc is meant to reach,
not output it has produced. The comparison passes while the two differ and fails once they match,
so the flag is dropped in the change that reaches the target. A refresh never rewrites a target.
"""

from __future__ import annotations

import difflib
import os
import pathlib
import re
import sys

_UPDATE_ENV = "LLVMDSDL_UPDATE_CODEGEN_SNAPSHOT"

_VERSION_PLACEHOLDER = "<LLVMDSDL_VERSION>"

_NORMALIZE_PATTERNS: tuple[tuple[re.Pattern[str], str], ...] = (
    (
        re.compile(
            r"^(\s*(?:#|//|/\*)\s*(?:Code generated|Generated) by llvmdsdl\s+)\d+\.\d+\.\d+(\b.*)$",
            re.MULTILINE,
        ),
        r"\1" + _VERSION_PLACEHOLDER + r"\2",
    ),
)


def _normalize_snapshot(text: str) -> str:
    normalized = text
    for pattern, replacement in _NORMALIZE_PATTERNS:
        normalized = pattern.sub(replacement, normalized)
    return normalized


def _load(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="strict")


def _wants_update() -> bool:
    return os.environ.get(_UPDATE_ENV, "") not in ("", "0")


def main(argv: list[str]) -> int:
    pending = "--pending" in argv[1:]
    paths = [arg for arg in argv[1:] if arg != "--pending"]
    if len(paths) != 2:
        sys.stderr.write("usage: compare_codegen_snapshot.py [--pending] <actual> <expected>\n")
        return 2

    actual_path = pathlib.Path(paths[0])
    expected_path = pathlib.Path(paths[1])
    actual = _normalize_snapshot(_load(actual_path))

    if pending:
        if actual == _load(expected_path):
            sys.stderr.write(f"target reached: {expected_path}\ndrop --pending from its comparison\n")
            return 1
        return 0

    if _wants_update():
        try:
            expected_path.write_text(actual, encoding="utf-8")
        except OSError as error:
            sys.stderr.write(f"snapshot golden: cannot write {expected_path}: {error}\n")
            return 1
        sys.stderr.write(f"snapshot golden rewritten: {expected_path}\n")
        return 0

    expected = _load(expected_path)

    if actual == expected:
        return 0

    diff = difflib.unified_diff(
        expected.splitlines(keepends=True),
        actual.splitlines(keepends=True),
        fromfile=str(expected_path),
        tofile=str(actual_path),
    )
    sys.stderr.write("snapshot mismatch after version normalisation:\n")
    sys.stderr.writelines(diff)
    sys.stderr.write(f"\nrefresh the golden with {_UPDATE_ENV}=1 on the lit run\n")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
