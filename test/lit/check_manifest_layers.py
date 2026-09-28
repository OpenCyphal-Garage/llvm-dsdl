#!/usr/bin/env python3
#===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===//

"""Read a generation run's naming manifest against an analysis run's.

A generation run's manifest reports the whole surface its lowering wrote for its target, and an
analysis run's reports the definition layer. The generation run adds names and changes none.

    check_manifest_layers.py subset ANALYSIS GENERATION
        Every key the analysis manifest holds for a language the generation manifest reports, the
        generation manifest holds with the same value.

    check_manifest_layers.py flat MANIFEST LANGUAGE DEFINITION
        Prints one line for each name the manifest reports for the definition, `path = value`, the
        path's keys joined by `/`.
"""

from __future__ import annotations

import json
import sys


def missing(expected, actual, path: str) -> list[str]:
    """The paths at which @p actual does not hold what @p expected does."""
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return [path]
        out = []
        for key, value in expected.items():
            if key not in actual:
                out.append(f"{path}/{key}")
            else:
                out.extend(missing(value, actual[key], f"{path}/{key}"))
        return out
    return [] if expected == actual else [path]


def subset(analysis_path: str, generation_path: str) -> int:
    with open(analysis_path, encoding="utf-8") as handle:
        analysis = json.load(handle)
    with open(generation_path, encoding="utf-8") as handle:
        generation = json.load(handle)
    failures = []
    for key in ("version", "tool", "type_name_versioning"):
        if analysis.get(key) != generation.get(key):
            failures.append(key)
    for language, definitions in generation["languages"].items():
        failures.extend(missing(analysis["languages"][language], definitions, language))
    for failure in failures:
        print(f"changed or dropped by the generation run: {failure}")
    return 1 if failures else 0


def flatten(value, path: str, out: list[str]) -> None:
    if isinstance(value, dict):
        for key in sorted(value):
            flatten(value[key], f"{path}/{key}" if path else key, out)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            flatten(item, f"{path}/{index}", out)
    else:
        out.append(f"{path} = {value}")


def flat(manifest_path: str, language: str, definition: str) -> int:
    with open(manifest_path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    lines: list[str] = []
    flatten(manifest["languages"][language][definition], "", lines)
    print("\n".join(lines))
    return 0


def main(argv: list[str]) -> int:
    if len(argv) == 4 and argv[1] == "subset":
        return subset(argv[2], argv[3])
    if len(argv) == 5 and argv[1] == "flat":
        return flat(argv[2], argv[3], argv[4])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
