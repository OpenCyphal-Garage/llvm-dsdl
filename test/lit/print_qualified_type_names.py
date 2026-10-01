#!/usr/bin/env python3
#===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===//

"""Print the qualified type names a language's naming manifest reports.

Each line is a definition's key, then a section's key where the name is a section's, then the name.
With --probe, print instead a C++ translation unit that includes every header under ROOT and names
every type the manifest reports, so that compiling it shows each name reaches a type.

    print_qualified_type_names.py MANIFEST LANGUAGE
    print_qualified_type_names.py MANIFEST LANGUAGE --probe ROOT
"""

from __future__ import annotations

import argparse
import json
import pathlib

SECTIONS = ("message", "request", "response")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("language")
    parser.add_argument("--probe", type=pathlib.Path)
    args = parser.parse_args()

    definitions = json.loads(args.manifest.read_text(encoding="utf-8"))["languages"][args.language]
    names = []
    for key, entry in sorted(definitions.items()):
        names.append((key, "", entry["qualified_type_name"]))
        names += [(key, section, entry[section]["qualified_type_name"]) for section in SECTIONS if section in entry]

    if args.probe is None:
        for key, section, name in names:
            print(" ".join(part for part in (key, section, name) if part))
        return 0

    for header in sorted(args.probe.rglob("*.hpp")):
        print(f'#include "{header.relative_to(args.probe).as_posix()}"')
    for index, (_, _, name) in enumerate(names):
        print(f"using probe_{index} = {name};")
    print("int main() { return 0; }")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
