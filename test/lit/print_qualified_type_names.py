#!/usr/bin/env python3
#===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===//

"""Print the qualified type names a language's naming manifest reports."""

from __future__ import annotations

import argparse
import json
import pathlib

SECTIONS = ("message", "request", "response")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Print each qualified type name a language's naming manifest reports, one per line: "
        "the definition's key, the section's key where the name is a section's, and the name."
    )
    parser.add_argument("manifest", type=pathlib.Path, help="the naming manifest dsdlc wrote")
    parser.add_argument("language", help="the language whose names are printed, as the manifest keys it")
    parser.add_argument(
        "--probe",
        type=pathlib.Path,
        metavar="ROOT",
        help="print instead a C++ translation unit that includes every header under ROOT and names every "
        "type the manifest reports, so that compiling it shows each name reaches a type",
    )
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
