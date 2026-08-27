#!/usr/bin/env python3
#===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===//

"""Check the naming manifest against the tree a backend actually wrote.

The manifest's whole claim is that it reports what will be generated rather than a second opinion
about it, so the test is not "does it contain plausible strings" but "does every stem it names for
Go exist as a file in the Go output".
"""

from __future__ import annotations

import json
import pathlib
import sys


def main() -> int:
    manifest_path, go_root = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    if manifest.get("version") != 1:
        print(f"unexpected manifest version: {manifest.get('version')!r}", file=sys.stderr)
        return 1

    languages = manifest["languages"]
    expected = {"c", "cpp", "go", "python", "rust", "ts"}
    if set(languages) != expected:
        print(f"an analysis target should report every language; got {sorted(languages)}", file=sys.stderr)
        return 1

    go = languages["go"]
    if not go:
        print("no definitions reported for go", file=sys.stderr)
        return 1

    failures = []
    for full_name, entry in sorted(go.items()):
        stem = entry["file_stem"]
        path = go_root.joinpath(*entry["namespace"], stem + ".go")
        if not path.is_file():
            failures.append(f"{full_name}: manifest names {path}, which was not generated")

    # The escaped cases are the point: a keyword type name and a keyword namespace component.
    claimed = go.get("fixtures_naming.naming.Claimed.1.0", {})
    fields = claimed.get("message", {}).get("fields", {})
    constants = claimed.get("message", {}).get("constants", {})
    if fields.get("serialize") != "Serialize_":
        failures.append(f"expected the field 'serialize' to be reported as Serialize_, got {fields.get('serialize')!r}")
    if constants.get("FULL_NAME") != "FULL_NAME_":
        failures.append(f"expected FULL_NAME to be reported as FULL_NAME_, got {constants.get('FULL_NAME')!r}")

    brk = go.get("fixtures_naming.naming.Break.1.0", {})
    if brk.get("file_stem") != "break__1_0":
        failures.append(f"expected Break to take the stem break__1_0, got {brk.get('file_stem')!r}")

    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
