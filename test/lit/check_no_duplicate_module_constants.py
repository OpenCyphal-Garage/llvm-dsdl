#!/usr/bin/env python3
# ===----------------------------------------------------------------------=== #
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------=== #

"""No generated Python or TypeScript module declares one name twice.

A Python module holds its classes, a service's alias and the service's own facts. A TypeScript
module holds each section's interface and the `const` of its name, a service's alias and the
service's own facts, and each `const` holds its section's facts, constants and functions.

A fixture can only pin the names that exist when it is written. This pins the property itself, so a
name added to an emitter and claimed nowhere is caught by what it does. In Python the second
assignment silently wins; in TypeScript a duplicate `export const` or a duplicate property of an
object literal does not compile.
"""

from __future__ import annotations

import pathlib
import re
import sys

PY_ASSIGNMENT = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(?::[^=]+)?=")
TS_DECLARATION = re.compile(r"^export (?:const|function) ([A-Za-z_$][A-Za-z0-9_$]*)\s*[=(]")
# A property of a type's `const`, which the object literal holds one indent in.
TS_PROPERTY = re.compile(r"^  ([A-Za-z_$][A-Za-z0-9_$]*)\s*[:(]")
TS_CONST_OPENS = re.compile(r"^export const [A-Za-z_$][A-Za-z0-9_$]* = \{$")


def duplicates(lines: list[str], pattern: re.Pattern[str]) -> list[str]:
    seen: set[str] = set()
    repeated: list[str] = []
    for line in lines:
        match = pattern.match(line)
        if not match:
            continue
        name = match.group(1)
        if name in seen:
            repeated.append(name)
        seen.add(name)
    return repeated


def python_duplicates(path: pathlib.Path) -> list[str]:
    return duplicates(path.read_text(encoding="utf-8").splitlines(), PY_ASSIGNMENT)


def typescript_duplicates(path: pathlib.Path) -> list[str]:
    """The module's declarations, then each `const`'s properties."""
    lines = path.read_text(encoding="utf-8").splitlines()
    repeated = duplicates(lines, TS_DECLARATION)
    body: list[str] | None = None
    for line in lines:
        if TS_CONST_OPENS.match(line):
            body = []
        elif body is not None and line.startswith("}"):
            repeated += duplicates(body, TS_PROPERTY)
            body = None
        elif body is not None:
            body.append(line)
    return repeated


def main() -> int:
    failures = []
    for root, suffix, check in (
        (pathlib.Path(sys.argv[1]), ".py", python_duplicates),
        (pathlib.Path(sys.argv[2]), ".ts", typescript_duplicates),
    ):
        files = sorted(root.rglob("*" + suffix))
        if not files:
            print(f"no {suffix} files under {root}", file=sys.stderr)
            return 1
        for path in files:
            for name in check(path):
                failures.append(f"{path.relative_to(root)} declares {name} more than once")

    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
