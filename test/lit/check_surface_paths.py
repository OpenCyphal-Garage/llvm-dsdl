#!/usr/bin/env python3
#===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===//

"""Check that every file a surface tree places a scope in is a file the generator wrote.

    check_surface_paths.py TREE OUTPUT_DIRECTORY
"""

from __future__ import annotations

import pathlib
import re
import sys

PATH = re.compile(r'^\s*dsdl\.scope \w+ "[^"]*" path = "([^"]*)"')


def main() -> int:
    tree = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    output = pathlib.Path(sys.argv[2])
    paths = [match.group(1) for match in map(PATH.match, tree.splitlines()) if match]
    missing = [path for path in paths if not (output / path).is_file()]
    for path in missing:
        print(f"the tree places a scope in {path}, which the generator did not write", file=sys.stderr)
    if not paths:
        print("the tree places no scope in a file", file=sys.stderr)
    return 1 if missing or not paths else 0


if __name__ == "__main__":
    sys.exit(main())
