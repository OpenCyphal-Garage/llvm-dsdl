#!/usr/bin/env python3
# ===----------------------------------------------------------------------=== #
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------=== #

"""What the two generators say about the same types, checked against each other.

The rest of the differential lane compares bytes. This compares the constants the two headers
declare beside them -- the extent, the largest representation, the port-ID, the number of options a
union has, an array field's capacity, and every constant the DSDL itself declares. They are what a
caller sizes a buffer and routes a transfer from, so a disagreement is wrong in a way no round-trip
would show.

The peer is corroboration, not an oracle, so this checks agreement on what the peer declares and
says nothing about what only we declare. A constant we emit and it does not is ours to define; a
constant we both emit and disagree on is a defect in one of us.

Neither header set can be compiled into one translation unit -- both publish
`uavcan/primitive/array/Real64_1_0.h`, and whichever include path comes first answers for both -- so
the comparison reads the headers rather than compiling them.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.*?)\s*$")

# The peer stamps its own build options into every header it writes. They describe how it was
# invoked, not what the type is.
PEER_ONLY_PREFIXES = ("NUNAVUT_SUPPORT_",)


def normalise(value: str) -> str:
    """Reduces a macro body to what it means.

    The two spell the same value differently: `(3U)` against `(3)`, and a `uint8` whose DSDL value
    was written as a character reaches us as `\'/\'` and the peer as `47`. Comparing the text would
    report a disagreement where there is none.
    """
    text = value.strip()
    while text.startswith("(") and text.endswith(")"):
        text = text[1:-1].strip()
    if text.startswith('"'):
        return text
    if text in ("true", "false"):
        return text
    character = re.fullmatch(r"\'(\\?.)\'", text)
    if character:
        literal = character.group(1)
        escapes = {"\\n": "\n", "\\t": "\t", "\\r": "\r", "\\0": "\0", "\\\\": "\\", "\\'": "'"}
        return str(ord(escapes.get(literal, literal[-1])))
    numeric = re.fullmatch(r"([+-]?(?:0[xX][0-9a-fA-F]+|\d+))[uUlL]*", text)
    if numeric:
        return str(int(numeric.group(1), 0))
    return text


def read_defines(path: pathlib.Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = DEFINE.match(line)
        if match and "(" not in match.group(1):
            out[match.group(1)] = match.group(2)
    return out


# Every generated type declares its own name, so every type prefix in a header is the text in front
# of one of these. Reading the prefixes out of the file rather than matching a shape is what makes
# this work for a service, which declares three of them, and for a namespace component the two
# generators strop differently.
ANCHORS = ("FULL_NAME_AND_VERSION_", "FULL_NAME_", "EXTENT_BYTES_")


def suffixes(defines: dict[str, str]) -> tuple[dict[str, str], set[str]]:
    """Keys each constant by its name with the type prefix removed, upper-cased.

    Upper-casing is what lets an array field's metadata line up: the peer spells it after the DSDL
    field name and we spell it after the macro projection of that name. Two constants on one side
    that differ only in case would be merged by it, so those keys are dropped and reported rather
    than compared wrongly.
    """
    prefixes = sorted(
        {name[: -len(anchor)] for name in defines for anchor in ANCHORS if name.endswith(anchor)},
        key=len,
        reverse=True,
    )
    out: dict[str, str] = {}
    ambiguous: set[str] = set()
    for name, value in defines.items():
        if name.endswith("_INCLUDED_") or name.startswith(PEER_ONLY_PREFIXES):
            continue
        prefix = next((p for p in prefixes if name.startswith(p)), "")
        key = name[len(prefix):].upper()
        if key in out and out[key] != value:
            ambiguous.add(key)
        out[key] = value
    return out, ambiguous


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ours", type=pathlib.Path, help="root of the llvm-dsdl C output")
    parser.add_argument("peer", type=pathlib.Path, help="root of the nunavut C output")
    args = parser.parse_args()

    failures: list[str] = []
    compared = 0
    types = 0

    for peer_header in sorted(args.peer.rglob("*.h")):
        relative = peer_header.relative_to(args.peer)
        if relative.parts[0] != "uavcan":
            continue
        # The peer writes `uavcan/_register/...` where we write `uavcan/register/...`: `register` is
        # a C keyword, and the two escape it in different places.
        ours_header = args.ours.joinpath(*(part.lstrip("_") for part in relative.parts))
        if not ours_header.exists():
            failures.append(f"{relative}: generated by the peer and not by us")
            continue
        types += 1

        peer_defines, peer_ambiguous = suffixes(read_defines(peer_header))
        ours_defines, ours_ambiguous = suffixes(read_defines(ours_header))
        skip = peer_ambiguous | ours_ambiguous

        for key, peer_value in sorted(peer_defines.items()):
            if key in skip:
                continue
            if key not in ours_defines:
                failures.append(f"{relative}: the peer declares {key} and we do not")
                continue
            compared += 1
            if normalise(peer_value) != normalise(ours_defines[key]):
                failures.append(
                    f"{relative}: {key} is {normalise(ours_defines[key])} for us "
                    f"and {normalise(peer_value)} for the peer"
                )

    if not types:
        print("no headers to compare; the corpus is empty", file=sys.stderr)
        return 1

    for failure in failures:
        print(failure, file=sys.stderr)
    print(f"compared {compared} constants over {types} types; {len(failures)} disagreements")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
