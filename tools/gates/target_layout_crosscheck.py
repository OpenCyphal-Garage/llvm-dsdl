#!/usr/bin/env python3
"""Checks the struct an object lowering derives against the one a C compiler lays out.

The object lane addresses a member by its position in a struct it derives from the schema, and
LLVM computes the offset from that struct and the target's data layout. The header a caller
includes is emitted by different code from the same model. Nothing makes the two agree; they are
expected to, and where they do not the object reads a field from the wrong bytes.

The host is only one target. A variable-length array holds its count in a `size_t`, so on a
32-bit target the count is half the width it is here and every member after it moves -- which a
probe compiled for the host cannot see. So the probe is written as `_Static_assert`s and compiled
for the target rather than run on it: nothing has to execute, and any target the compiler can
name can be checked.

Two things are asserted, per member, against the derived struct:

  * a member is the width the derived struct gives it -- the count of a variable-length array,
    one element of one, and every scalar field;
  * the members are laid out in the order the derivation addresses them.

A nested composite's own members are checked in that type's own row.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SCHEMA = re.compile(r'dsdl\.schema @(\S+)')
PLAN = re.compile(r'dsdl\.serialization_plan attributes \{([^}]*)\}')
ATTR_STR = re.compile(r'(\w+) = "((?:[^"\\]|\\.)*)"')
FUNCTION = re.compile(r'func\.func @(\S+?)\(')
STRUCT_TYPE = re.compile(r'!llvm\.struct<\((.*)\)>$')

STRUCT = re.compile(r'typedef struct (\w+) \{(.*?)\n\} \1[^;]*;', re.S)
MEMBER = re.compile(r'^\s*[A-Za-z_][\w \*]*?(\w+)\s*(?:\[[^\]]*\])?\s*;\s*$')
CLOSING_MEMBER = re.compile(r'^\s*\}\s*(\w+)\s*(?:\[[^\]]*\])?\s*;\s*$')

SCALAR_BYTES = {"i8": 1, "i16": 2, "i32": 4, "i64": 8, "f32": 4, "f64": 8, "i1": 1}


def split_members(body: str) -> list[str]:
    """The comma-separated members of one LLVM struct body, respecting nesting."""
    out: list[str] = []
    depth = 0
    current = ""
    for ch in body:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if (ch == ",") and (depth == 0):
            out.append(current.strip())
            current = ""
            continue
        current += ch
    if current.strip():
        out.append(current.strip())
    return out


@dataclass
class Derived:
    """One member of a derived struct, as far as a C probe can reach it."""

    kind: str  # "scalar", "array", or "composite"
    width: int = 0  # bytes, for scalar and for one array element
    count_width: int = 0  # bytes of the count beside a variable-length array's elements
    elements: str = ""  # the name the C struct gives the element storage


def describe(member: str) -> Derived:
    """What a probe can assert about one derived member."""
    if member in SCALAR_BYTES:
        return Derived("scalar", SCALAR_BYTES[member])
    array = re.match(r'^struct<\(array<(\d+) x ([^>]+(?:>)*)>, (i\d+)\)>$', member)
    if array:
        element = array.group(2).strip()
        if element in SCALAR_BYTES:
            return Derived("array",
                           SCALAR_BYTES[element],
                           SCALAR_BYTES[array.group(3)],
                           "elements")
        # An array of composites: the elements are checked in the nested type's own row, but the
        # count beside them is this struct's.
        return Derived("array", 0, SCALAR_BYTES[array.group(3)], "elements")
    return Derived("composite")


def parse_type_names(schema_text: str) -> dict[str, str]:
    """`<symbol><section>` -> the C type name, so a body can be tied to a struct."""
    names: dict[str, str] = {}
    symbol = ""
    for line in schema_text.splitlines():
        found = SCHEMA.search(line)
        if found:
            symbol = found.group(1)
        plan = PLAN.search(line)
        if plan and symbol:
            values = dict(ATTR_STR.findall(plan.group(1)))
            section = values.get("section", "")
            suffix = ("__" + section) if section else ""
            if "c_type_name" in values:
                names[symbol + suffix] = values["c_type_name"]
    return names


def parse_derived(converted_text: str) -> dict[str, list[str]]:
    """`<symbol><section>` -> the members of the struct its bodies address within."""
    derived: dict[str, list[str]] = {}
    current = ""
    for line in converted_text.splitlines():
        found = FUNCTION.search(line)
        if found:
            current = found.group(1)
        shape = STRUCT_TYPE.search(line.strip())
        if shape and current.endswith("_ir_"):
            stem = current.rsplit("__", 1)[0]
            for tail in ("__serialize", "__deserialize"):
                if stem.endswith(tail):
                    stem = stem[: -len(tail)]
            derived.setdefault(stem, split_members(shape.group(1)))
    return derived


def parse_structs(root: Path) -> dict[str, list[str]]:
    """The members each generated struct declares, in order."""
    structs: dict[str, list[str]] = {}
    for header in root.rglob("*.h"):
        text = header.read_text(encoding="utf-8", errors="replace")
        for match in STRUCT.finditer(text):
            members: list[str] = []
            depth = 0
            for line in match.group(2).splitlines():
                stripped = line.strip()
                if stripped.startswith("struct {"):
                    depth += 1
                    continue
                if depth > 0:
                    if stripped.startswith("}"):
                        depth -= 1
                        closing = CLOSING_MEMBER.match(line)
                        if closing:
                            members.append(closing.group(1))
                    continue
                member = MEMBER.match(line)
                if member:
                    members.append(member.group(1))
            structs[match.group(1)] = members
    return structs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--c-root", required=True, type=Path)
    parser.add_argument("--schema", required=True, type=Path)
    parser.add_argument("--converted", required=True, type=Path)
    parser.add_argument("--target", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--workdir", required=True, type=Path)
    args = parser.parse_args()

    names = parse_type_names(args.schema.read_text(encoding="utf-8"))
    derived = parse_derived(args.converted.read_text(encoding="utf-8"))
    structs = parse_structs(args.c_root)

    lines = ["#include <stddef.h>", "#include <stdint.h>", "#include <stdbool.h>"]
    lines += [f'#include "{path.as_posix()}"'
              for path in sorted(p.relative_to(args.c_root) for p in args.c_root.rglob("*.h"))]

    checked = 0
    members_checked = 0
    for stem, members in sorted(derived.items()):
        name = names.get(stem)
        declared = structs.get(name) if name else None
        if (declared is None) or (len(declared) != len(members)):
            continue
        checked += 1
        previous = ""
        for index, (member, shape) in enumerate(zip(declared, members)):
            what = describe(shape)
            if what.kind == "scalar":
                lines.append(f'_Static_assert(sizeof((({name}*) 0)->{member}) == {what.width}u, '
                             f'"{name}.{member}: derived {what.width} byte(s)");')
                members_checked += 1
            elif what.kind == "array":
                lines.append(f'_Static_assert(sizeof((({name}*) 0)->{member}.count) == '
                             f'{what.count_width}u, "{name}.{member}.count: derived '
                             f'{what.count_width} byte(s)");')
                members_checked += 1
                if what.width:
                    # A bool array is bitpacked, so its storage is named for that rather than for
                    # its elements.
                    storage = "bitpacked" if _is_bitpacked(args.c_root, name, member) else "elements"
                    lines.append(f'_Static_assert(sizeof((({name}*) 0)->{member}.{storage}[0]) == '
                                 f'{what.width}u, "{name}.{member}: derived element {what.width} '
                                 f'byte(s)");')
                    members_checked += 1
            if previous:
                lines.append(f'_Static_assert(offsetof({name}, {member}) > '
                             f'offsetof({name}, {previous}), "{name}: member {index} ({member}) is '
                             f'not after the one before it");')
            previous = member

    if checked == 0:
        print("target layout cross-check matched no types; the derivation was not read")
        return 1

    args.workdir.mkdir(parents=True, exist_ok=True)
    probe = args.workdir / "layout.c"
    probe.write_text("\n".join(lines) + "\n", encoding="utf-8")

    # A target with no sysroot has only the compiler's own freestanding headers, and the runtime
    # header the generated types include reaches past those. The probe never calls any of it --
    # it asks the compiler for sizes and offsets -- so declaring what is named is enough. These
    # are searched last, so a target that has the real ones uses those instead.
    shim = args.workdir / "shim"
    shim.mkdir(parents=True, exist_ok=True)
    (shim / "string.h").write_text(
        "#pragma once\n#include <stddef.h>\n"
        "void* memmove(void*, const void*, size_t);\n"
        "void* memset(void*, int, size_t);\n"
        "void* memcpy(void*, const void*, size_t);\n", encoding="utf-8")
    (shim / "math.h").write_text(
        "#pragma once\n#define isfinite(x) __builtin_isfinite(x)\n", encoding="utf-8")
    (shim / "assert.h").write_text(
        "#pragma once\n#define assert(x) ((void) 0)\n", encoding="utf-8")

    compiled = subprocess.run(
        [args.clang, f"--target={args.target}", "-ffreestanding", "-std=c11",
         "-I", str(args.c_root), "-idirafter", str(shim),
         "-c", "-o", "/dev/null", str(probe)],
        capture_output=True, text=True, check=False)
    if compiled.returncode != 0:
        print(f"target layout cross-check failed for {args.target}: the struct an object "
              "addresses is not the one the compiler lays out")
        print(compiled.stdout + compiled.stderr)
        return 1

    print(f"target layout cross-check ({args.target}): {checked} derived struct(s) and "
          f"{members_checked} member(s) lay out as the object addresses them")
    return 0


def _is_bitpacked(root: Path, type_name: str, member: str) -> bool:
    """Whether @p member is declared as a bitpacked array rather than as elements."""
    for header in root.rglob("*.h"):
        text = header.read_text(encoding="utf-8", errors="replace")
        if f"typedef struct {type_name} {{" not in text:
            continue
        body = text.split(f"typedef struct {type_name} {{", 1)[1].split(f"}} {type_name}", 1)[0]
        for block in body.split("struct {")[1:]:
            head, _, tail = block.partition("}")
            if tail.strip().startswith(member):
                return "bitpacked" in head
    return False


if __name__ == "__main__":
    sys.exit(main())
