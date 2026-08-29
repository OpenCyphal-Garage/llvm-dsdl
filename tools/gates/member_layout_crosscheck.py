#!/usr/bin/env python3
"""Checks the member positions a plan carries against the struct the C backend emits.

An object lowering addresses a member by its position, because it has no name to use. That
position is computed from the schema -- the non-padding fields in declaration order, and a
union's options before its `_tag_` -- while the struct it has to agree with is emitted from
the semantic model by a different piece of code. Nothing makes the two agree; they are simply
expected to, and an offset that disagrees is not a crash but a field read from the wrong
bytes.

So this compares them, three ways:

  * the members the header declares are exactly the non-padding fields the schema lists, in
    that order, with a union's `_tag_` last;
  * a `void` field contributes no member at all;
  * the offsets of those members, in that order, strictly increase -- which C guarantees for
    declaration order and which therefore catches anything that has quietly stopped being
    declaration order;
  * each member is the width its DSDL type implies -- an eleven-bit field is held in two bytes,
    a float16 in a `float`, and an array's element in whatever one element takes. An offset
    computed from a wrong width lands between fields rather than on one.

The first two are read off the two generated artefacts. The third is compiled and run, because
`offsetof` is the only thing that answers what the layout actually is.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# `dsdl.field {c_name = "x", name = "x", padding, section = "request", type_name = "..."}`
FIELD = re.compile(r'dsdl\.field \{([^}]*)\}')
SCHEMA = re.compile(r'dsdl\.schema @(\S+)\s+attributes\s*\{([^}]*)\}')
# `is_union` is carried by the plan, not the schema: a service is a union in one section and
# not in the other, so it is not a property of the type.
PLAN = re.compile(r'dsdl\.serialization_plan attributes \{([^}]*)\}')
ATTR_STR = re.compile(r'(\w+) = "((?:[^"\\]|\\.)*)"')

# `typedef struct NAME {` ... `} NAME;`
STRUCT = re.compile(r'typedef struct (\w+) \{(.*?)\n\} \1;', re.S)
# A member declaration is the last identifier before `;`, with any array extent after it.
MEMBER = re.compile(r'^\s*[A-Za-z_][\w \*]*?(\w+)\s*(?:\[[^\]]*\])?\s*;\s*$')
# A variable-length array is an anonymous struct, so its member name is on the closing brace.
CLOSING_MEMBER = re.compile(r'^\s*\}\s*(\w+)\s*(?:\[[^\]]*\])?\s*;\s*$')


# `saturated uint11`, `truncated uint40`, `bool`, `saturated float16[<=64]`.
PRIMITIVE = re.compile(r'^(?:saturated |truncated )?(uint|int|float|bool)(\d+)?(\[(<=?)?[^\]]*\])?$')


def scalar_size(kind: str, bits: int) -> int:
    """Bytes the C backend holds one value of this type in.

    A DSDL width is not a C width: eleven bits are held in two bytes, and a float16 in a
    `float`, there being no narrower one to put it in.
    """
    if kind == "bool":
        return 1
    if kind == "float":
        return 4 if bits <= 32 else 8
    holder = 8 if bits <= 8 else 16 if bits <= 16 else 32 if bits <= 32 else 64
    return holder // 8


@dataclass
class Member:
    """One struct member, and what the schema says its storage should be."""

    name: str
    kind: str = ""
    bits: int = 0
    array: str = ""  # "", "fixed" or "variable"


@dataclass
class Section:
    """One struct's worth of schema: the fields it declares, in order."""

    members: list[Member] = field(default_factory=list)
    is_union: bool = False


def parse_schema(text: str) -> dict[tuple[str, str], Section]:
    """The non-padding field names each schema section declares, keyed by (full name, section)."""
    sections: dict[tuple[str, str], Section] = {}
    for match in SCHEMA.finditer(text):
        attrs = dict(ATTR_STR.findall(match.group(2)))
        full = attrs.get("full_name", match.group(1))
        body_start = match.end()
        body_end = text.find("\n  }", body_start)
        body = text[body_start:body_end if body_end > 0 else len(text)]
        for plan in PLAN.finditer(body):
            inner = plan.group(1)
            values = dict(ATTR_STR.findall(inner))
            key = (full, values.get("section", ""))
            sections.setdefault(key, Section()).is_union = "is_union" in inner

        for raw in FIELD.finditer(body):
            inner = raw.group(1)
            values = dict(ATTR_STR.findall(inner))
            section = values.get("section", "")
            key = (full, section)
            entry = sections.setdefault(key, Section())
            if ", padding" in inner or inner.startswith("padding"):
                continue
            name = values.get("c_name", "")
            match_primitive = PRIMITIVE.match(values.get("type_name", ""))
            if match_primitive is None:
                # A composite: its own storage is checked in its own row.
                entry.members.append(Member(name))
                continue
            kind, bits, extent, bounded = (match_primitive.group(1),
                                           match_primitive.group(2),
                                           match_primitive.group(3),
                                           match_primitive.group(4))
            entry.members.append(Member(name,
                                        kind,
                                        int(bits) if bits else 1,
                                        "" if not extent else ("variable" if bounded else "fixed")))
    return sections


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
                # A variable-length array is an anonymous struct; its own members are not the
                # outer struct's, so only the name it is closed with counts.
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


def c_type_name(full_name: str, section: str) -> str:
    base = full_name.replace(".", "__")
    if section == "request":
        return base + "__Request"
    if section == "response":
        return base + "__Response"
    return base


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dsdlc", required=True)
    parser.add_argument("--c-root", required=True, type=Path)
    parser.add_argument("--mlir", required=True, type=Path)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--workdir", required=True, type=Path)
    args = parser.parse_args()

    sections = parse_schema(args.mlir.read_text(encoding="utf-8"))
    structs = parse_structs(args.c_root)

    failures: list[str] = []
    checked = 0
    offset_cases: list[tuple[str, list[str], Path]] = []
    width_cases: list[tuple[str, list[Member], Path]] = []

    for (full, section), entry in sorted(sections.items()):
        name = c_type_name(full, section)
        declared = structs.get(name)
        if declared is None:
            continue
        expected_members = list(entry.members)
        if entry.is_union:
            expected_members.append(Member("_tag_"))
        expected = [m.name for m in expected_members]
        if not expected:
            # C has no empty struct, so a type with nothing to hold is given a member no field
            # maps to. Nothing addresses it, and it takes no position from anything.
            expected = ["_dummy_"]
        checked += 1
        if declared != expected:
            failures.append(f"{name}:\n      schema says {expected}\n      struct has  {declared}")
            continue
        header = next((h for h in args.c_root.rglob("*.h")
                       if f"typedef struct {name} {{" in h.read_text(encoding="utf-8", errors="replace")), None)
        if header is not None and declared:
            offset_cases.append((name, declared, header.relative_to(args.c_root)))
            width_cases.append((name, expected_members, header.relative_to(args.c_root)))

    if failures:
        print("member layout cross-check failed: the position a plan carries would address the "
              "wrong member")
        for failure in failures[:12]:
            print(f"    {failure}")
        print(f"  {len(failures)} of {checked} type(s) disagree")
        return 1

    # What the two artefacts say is one thing; what the compiler lays out is another.
    args.workdir.mkdir(parents=True, exist_ok=True)
    driver = args.workdir / "offsets.c"
    lines = ["#include <stddef.h>", "#include <stdio.h>"]
    includes = sorted({case[2].as_posix() for case in offset_cases})
    lines += [f'#include "{path}"' for path in includes]
    lines += ["int main(void)", "{", "    size_t previous = 0U;", "    int failures = 0;"]
    for name, members, _ in offset_cases:
        lines.append("    previous = 0U;")
        for index, member in enumerate(members):
            lines.append(f"    if (offsetof({name}, {member}) < previous) {{")
            lines.append(f'        printf("{name}: member {index} (%s) is laid out before the one '
                         f'before it\\n", "{member}");')
            lines.append("        ++failures;")
            lines.append("    }")
            lines.append(f"    previous = offsetof({name}, {member});")

    # And what each member is made of. A scalar is held in its holder width, an array element
    # in one element's worth, and a bool array bitpacked -- which is why it is measured as
    # bytes of storage rather than as a count of elements.
    for name, members, _ in width_cases:
        for member in members:
            if not member.kind:
                continue
            expected_size = scalar_size(member.kind, member.bits)
            if member.array == "":
                probe = f"sizeof(((({name}*) 0)->{member.name}))"
            elif member.kind == "bool":
                # Bitpacked: one bit per element, so the element width is not a thing to ask
                # about. That it is bytes at all is the property worth holding.
                probe = (f"sizeof(((({name}*) 0)->{member.name}[0]))" if member.array == "fixed"
                         else f"sizeof(((({name}*) 0)->{member.name}.bitpacked[0]))")
                expected_size = 1
            elif member.array == "fixed":
                probe = f"sizeof(((({name}*) 0)->{member.name}[0]))"
            else:
                probe = f"sizeof(((({name}*) 0)->{member.name}.elements[0]))"
            lines.append(f"    if ({probe} != {expected_size}U) {{")
            lines.append(f'        printf("{name}.{member.name}: schema implies {expected_size} '
                         f'byte(s), struct holds %zu\\n", (size_t) {probe});')
            lines.append("        ++failures;")
            lines.append("    }")

    lines += ["    if (failures == 0) { printf(\"ok\\n\"); }", "    return failures == 0 ? 0 : 1;", "}"]
    driver.write_text("\n".join(lines) + "\n", encoding="utf-8")

    binary = args.workdir / "offsets"
    compiled = subprocess.run([args.cc, "-std=c11", "-I", str(args.c_root), "-o", str(binary), str(driver)],
                              capture_output=True, text=True, check=False)
    if compiled.returncode != 0:
        print("member layout cross-check could not build its offset probe:")
        print(compiled.stdout + compiled.stderr)
        return 1
    executed = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
    if executed.returncode != 0:
        print("member layout cross-check: the struct is not laid out the way the schema implies")
        print(executed.stdout + executed.stderr)
        return 1

    widths = sum(1 for _, members, _ in width_cases for m in members if m.kind)
    print(f"member layout cross-check: {checked} type(s) agree on member order, "
          f"{len(offset_cases)} struct(s) lay out in that order, and {widths} member(s) are the "
          "width their type implies")
    return 0


if __name__ == "__main__":
    sys.exit(main())
