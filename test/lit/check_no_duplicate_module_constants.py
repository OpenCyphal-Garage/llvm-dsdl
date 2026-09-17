#!/usr/bin/env python3
# ===----------------------------------------------------------------------=== #
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------=== #

"""No generated Python or TypeScript module declares one name twice.

Python and TypeScript put a definition's own facts and its sections' constants in one scope, so a
type whose constant prefix is the module's own prefix can reach them. `moduleMetadataNames` in
`lib/CodeGen/SectionNaming.cpp` lists what the module owns, and that list is a second copy of what
the two emitters write.

A fixture can only pin the names that exist when it is written. This pins the property the list
exists to hold, so a name added to an emitter and forgotten there is caught by what it does rather
than by being absent from a list. In Python the second assignment silently wins; in TypeScript a
duplicate `export const` or `export function` does not compile.
"""

from __future__ import annotations

import pathlib
import re
import sys

PY_ASSIGNMENT = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(?::[^=]+)?=")
# A function is a declaration in the same scope as a const, and a factory is a function: two
# `export function makeX` in one module do not compile either.
TS_ASSIGNMENT = re.compile(r"^export (?:const|function) ([A-Za-z_$][A-Za-z0-9_$]*)\s*[=(]")


def duplicates(path: pathlib.Path, pattern: re.Pattern[str]) -> list[str]:
    seen: set[str] = set()
    repeated: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        name = match.group(1)
        if name in seen:
            repeated.append(name)
        seen.add(name)
    return repeated


def main() -> int:
    failures = []
    for root, suffix, pattern in (
        (pathlib.Path(sys.argv[1]), ".py", PY_ASSIGNMENT),
        (pathlib.Path(sys.argv[2]), ".ts", TS_ASSIGNMENT),
    ):
        files = sorted(root.rglob("*" + suffix))
        if not files:
            print(f"no {suffix} files under {root}", file=sys.stderr)
            return 1
        for path in files:
            for name in duplicates(path, pattern):
                failures.append(f"{path.relative_to(root)} declares {name} more than once")

    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
