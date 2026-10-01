#!/usr/bin/env python3
#===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===//

"""Check a language's surface tree against the naming manifest.

The tree and the manifest both come from `allocateSurface`, so every name the manifest reports for a
definition is a name the tree declares for it: the type names, the file stem, the namespace, and each
field, constant and union option tag. A qualified type name is the type's name qualified by the
namespaces and the type that enclose it in the tree.

    check_surface_manifest.py MANIFEST LANGUAGE TREE
"""

from __future__ import annotations

import json
import pathlib
import re
import sys

SCOPE = re.compile(r'^(\s*)dsdl\.scope (\w+) "([^"]*)"(.*)\{$')
DECL = re.compile(r'^(\s*)dsdl\.decl "([^"]*)" kind = (\w+)(.*)$')
OF = re.compile(r'of = @([\w.]+)')
SECTION = re.compile(r'section = "([^"]*)"')
MEMBER = re.compile(r'member = "([^"]*)"')
ORIGIN = re.compile(r'origin = (\w+)')

# What joins a qualifier to the name it qualifies, in each language whose manifest reports a type's
# name qualified, as the language's row states it.
SEPARATOR = {"c": "", "cpp": "::"}


def parse(text: str) -> list[dict]:
    """The tree's scopes and declarations, each with the chain of scopes above it."""
    items = []
    stack: list[tuple[int, dict]] = []
    for line in text.splitlines():
        scope = SCOPE.match(line)
        decl = DECL.match(line)
        match = scope or decl
        if not match:
            continue
        depth = len(match.group(1))
        while stack and stack[-1][0] >= depth:
            stack.pop()
        rest = match.group(4)
        of = OF.search(rest)
        section = SECTION.search(rest)
        member = MEMBER.search(rest)
        origin = ORIGIN.search(rest)
        item = {
            "scope": bool(scope),
            "kind": match.group(2) if scope else match.group(3),
            "name": match.group(3) if scope else match.group(2),
            "of": of.group(1) if of else "",
            "section": section.group(1) if section else "",
            "member": member.group(1) if member else "",
            "generated": bool(origin and origin.group(1) == "generated"),
            "parents": [entry for _, entry in stack],
        }
        items.append(item)
        if scope:
            stack.append((depth, item))
    return items


def main() -> int:
    manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    language = sys.argv[2]
    items = parse(pathlib.Path(sys.argv[3]).read_text(encoding="utf-8"))
    definitions = manifest["languages"][language]
    failures = []

    def named(key: str, section: str, **kinds) -> list[dict]:
        return [item for item in items if item["of"] == key and item["section"] == section and all(
            item[field] == value for field, value in kinds.items())]

    for key, entry in sorted(definitions.items()):
        types = named(key, "", scope=True, kind="type") + named(key, "", scope=False, kind="alias")
        # A type declared apart is published under its alias, which is the name that qualifies it.
        aliases = [item["name"] for item in types if item["kind"] == "alias"]
        public = aliases[0] if aliases else next((item["name"] for item in types), "")

        def qualified(item: dict) -> str:
            """@p item's name, qualified by the namespaces and the type that enclose it in the tree."""
            parts = [public if parent["kind"] == "type" else parent["name"] for parent in item["parents"]
                     if parent["kind"] in ("namespace", "module", "package", "type")]
            return SEPARATOR.get(language, "").join(parts + [item["name"]])

        type_names = {item["name"] for item in types}
        if "type_name" in entry and entry["type_name"] not in type_names:
            failures.append(f"{key}: the tree declares no type {entry['type_name']!r}, only {sorted(type_names)}")
        if "qualified_type_name" in entry and entry["qualified_type_name"] not in {qualified(item) for item in types}:
            failures.append(f"{key}: the tree declares no type {entry['qualified_type_name']!r}, "
                            f"only {sorted(qualified(item) for item in types)}")

        sections = [s for s in ("message", "request", "response") if s in entry]
        for section_key in sections:
            section = "" if section_key == "message" else section_key
            reported = entry[section_key]
            section_types = named(key, section, scope=True, kind="type")
            if not section_types:
                failures.append(f"{key} {section_key}: the tree has no type scope")
                continue
            file_scope = next((p for p in reversed(section_types[0]["parents"]) if p["kind"] in ("file", "module")), None)
            if file_scope is None or file_scope["name"] != entry["file_stem"]:
                failures.append(f"{key}: the tree's file is {file_scope and file_scope['name']!r}, "
                                f"the manifest's {entry['file_stem']!r}")
            namespaces = [p["name"] for p in section_types[0]["parents"] if p["kind"] in ("namespace", "module", "package")
                          and p is not file_scope]
            if namespaces and namespaces != entry["namespace"]:
                failures.append(f"{key}: the tree's namespace is {namespaces}, the manifest's {entry['namespace']}")
            section_names = section_types + named(key, section, scope=False, kind="alias")
            if "type_name" in reported and reported["type_name"] not in {item["name"] for item in section_names}:
                failures.append(f"{key} {section_key}: the tree declares no type {reported['type_name']!r}")
            if "qualified_type_name" in reported and reported["qualified_type_name"] not in {
                    qualified(item) for item in section_names}:
                failures.append(f"{key} {section_key}: the tree declares no type {reported['qualified_type_name']!r}")
            for dsdl, name in reported.get("fields", {}).items():
                found = [item["name"] for item in named(key, section, scope=False, kind="field", member=dsdl)]
                if found != [name]:
                    failures.append(f"{key} {section_key}: field {dsdl} is {found} in the tree, {name!r} in the manifest")
            for dsdl, name in reported.get("constants", {}).items():
                found = [item["name"] for item in named(key, section, scope=False, kind="constant", member=dsdl)
                         if not item["generated"]]
                if found != [name]:
                    failures.append(f"{key} {section_key}: constant {dsdl} is {found} in the tree, {name!r} in the manifest")
            for dsdl, option in reported.get("union_options", {}).items():
                found = [item["name"] for item in named(key, section, scope=False, kind="option", member=dsdl)]
                if found != [option["name"]]:
                    failures.append(f"{key} {section_key}: option {dsdl} is {found} in the tree, "
                                    f"{option['name']!r} in the manifest")

    if not definitions:
        failures.append(f"the manifest reports no definition for {language}")
    for failure in failures:
        print(f"{language}: {failure}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
