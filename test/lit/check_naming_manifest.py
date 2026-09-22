#!/usr/bin/env python3
#===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===//

"""Check the naming manifest against the tree a backend wrote.

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

    # The stem is projected from the versioned name, so the keyword `break` is not what the escape
    # sees and nothing is escaped. The manifest reports the file on disk, which is what a build
    # integration references.
    # Rust names a type after its short name and lets its module carry the namespace, so the
    # manifest reports the type name for it as it does for the other three module-scoped languages.
    rust = languages.get("rust", {})
    case_folds = rust.get("fixtures_naming.naming.CaseFolds.1.0", {})
    if case_folds.get("type_name") != "CaseFolds":
        failures.append(
            f"expected the rust type name for CaseFolds to be reported as CaseFolds, "
            f"got {case_folds.get('type_name')!r}"
        )

    # A section's name does not follow from the definition's in Rust -- the module carries the
    # service, so the section word alone is the name -- so it is reported on the section too.
    svc = rust.get("fixtures_naming.naming.Call.1.0", {})
    got = (svc.get("request", {}).get("type_name"), svc.get("response", {}).get("type_name"))
    if got != ("Request", "Response"):
        failures.append(f"expected the rust section type names to be Request/Response, got {got!r}")

    brk = go.get("fixtures_naming.naming.Break.1.0", {})
    if brk.get("file_stem") != "break_1_0":
        failures.append(f"expected Break to take the stem break_1_0, got {brk.get('file_stem')!r}")

    # A union option's tag is the one fact about it that cannot be read off the generated type, so
    # the manifest is the only place a build integration can get it. Both halves are checked: the
    # name, which comes from the shared projection, and the value, which comes from the lowered
    # schema. OptionTags declares its options after two DSDL constants named for their tags, so the
    # projection has to move those and leave the options alone.
    options = go.get("fixtures_naming.naming.OptionTags.1.0", {}).get("message", {}).get("union_options")
    if options is None:
        failures.append("expected OptionTags to report union_options")
    else:
        expected_options = {
            "fooBar": {"name": "FOO_BAR_OPTION_TAG", "tag": 0},
            "foo_bar": {"name": "FOO_BAR_OPTION_TAG_2", "tag": 1},
            "other": {"name": "OTHER_OPTION_TAG", "tag": 2},
        }
        if options != expected_options:
            failures.append(f"union_options for OptionTags is {options!r}, expected {expected_options!r}")

    # A structure has no options to report, and reporting an empty map would read as a union with
    # none rather than as a type that is not one.
    if "union_options" in go.get("fixtures_naming.naming.Claimed.1.0", {}).get("message", {}):
        failures.append("Claimed is not a union and should report no union_options")

    ported = go.get("fixtures_naming.naming.Call.1.0", {})
    if "fixed_port_id" in ported:
        failures.append("Call has no fixed port-ID and should report none")

    # A type named DSDL is the one case where a section constant is in reach of the module's own
    # names in Python and TypeScript. The manifest reports what is written, so it has to report the
    # moved name; reporting the unescaped one would name a constant the module does not define.
    for language in ("python", "ts"):
        constants = (
            manifest["languages"][language]
            .get("fixtures_naming.naming.DSDL.1.0", {})
            .get("message", {})
            .get("constants", {})
        )
        for source in ("FULL_NAME", "HAS_FIXED_PORT_ID", "FIXED_PORT_ID", "VERSION_MAJOR"):
            reported = constants.get(source)
            if reported != source + "_2":
                failures.append(
                    f"{language}: DSDL's constant {source} is reported as {reported!r}, "
                    f"expected {source + '_2'!r}"
                )

    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
