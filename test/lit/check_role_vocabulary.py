#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#

"""Hold the role names a plan body is stamped with to the ones the translator reads back.

`build-dsdl-plan-bodies` writes `llvmdsdl.result_roles` and `lib/CodeGen/BodyTranslator.cpp` reads
it, each spelling the names itself. A name only one of them knows costs nothing at either end: the
translator answers with no role, the value reaches the generated source numbered rather than named,
and every other gate stays green. Adding a name to the pass means teaching the translator to read
it, and this is what says so.
"""

from __future__ import annotations

import pathlib
import re
import sys

#: The names lib/CodeGen/BodyTranslator.cpp answers to, in stampedRole.
READ_BACK = {"offset", "error", "rejected", "size"}

_STAMP = re.compile(r"llvmdsdl\.result_roles\s*=\s*\[([^\]]*)\]")
_NAME = re.compile(r'"([^"]*)"')


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        sys.stderr.write("usage: check_role_vocabulary.py <lowered.mlir>\n")
        return 2

    text = pathlib.Path(argv[1]).read_text(encoding="utf-8")
    stamped = {name for stamp in _STAMP.findall(text) for name in _NAME.findall(stamp)}
    if not stamped:
        sys.stderr.write(f"no llvmdsdl.result_roles stamp in {argv[1]}; the pass stopped writing them\n")
        return 1

    unread = sorted(stamped - READ_BACK)
    if unread:
        sys.stderr.write(
            f"stamped with {unread}, which lib/CodeGen/BodyTranslator.cpp does not read back.\n"
            f"Teach stampedRole the name, then add it to READ_BACK here.\n"
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
