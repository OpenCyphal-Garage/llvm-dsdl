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


# The peer prefixes an underscore to a path component that would otherwise be a C keyword. Only
# those are undone; every other leading underscore is part of the name.
C_KEYWORDS = frozenset(
    """auto break case char const continue default do double else enum extern float for goto if
    inline int long register restrict return short signed sizeof static struct switch typedef union
    unsigned void volatile while""".split()
)


def unescape_keyword(component: str) -> str:
    """Undoes the peer's keyword escape on one path component, and nothing else."""
    if not component.startswith("_"):
        return component
    stem = component[1:]
    return stem if stem.split(".", 1)[0] in C_KEYWORDS else component


def read_defines(path: pathlib.Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = DEFINE.match(line)
        if match and "(" not in match.group(1):
            out[match.group(1)] = match.group(2)
    return out


# Every generated type declares its own name, so a type prefix in a header is the text in front of
# this. Reading the prefixes out of the file rather than matching a shape is what makes this work for
# a namespace component the two generators strop differently.
ANCHOR = "FULL_NAME_"


def suffixes(defines: dict[str, str]) -> tuple[dict[tuple[str, str], str], set[tuple[str, str]]]:
    """Keys each constant by the type that declares it and its own name, upper-cased.

    A header can declare several types -- a service declares three, itself and its two sections --
    and each spells the same constants under its own prefix. Keying by the name alone would collapse
    a service's three `FULL_NAME_` onto one and compare whichever came last, which silently drops the
    sections' extents from the comparison. The type is identified by the value of its `FULL_NAME_`,
    which is the one thing both generators spell identically and which no prefix projection affects.

    Upper-casing the rest is what lets an array field's metadata line up: the peer spells it after
    the DSDL field name and we spell it after the macro projection of that name. Two constants of one
    type that differ only in case would be merged by it, so those keys are returned separately for
    the caller to report rather than compared wrongly.
    """
    # Longest first: `<base>_FULL_NAME_` is a prefix of nothing, but `<base>_` is a prefix of
    # `<base>_Request_...`, so the longest match is the one that owns the constant.
    prefixes = sorted(
        {name[: -len(ANCHOR)] for name in defines if name.endswith(ANCHOR)},
        key=len,
        reverse=True,
    )
    owner = {prefix: defines[prefix + ANCHOR] for prefix in prefixes}

    out: dict[tuple[str, str], str] = {}
    ambiguous: set[tuple[str, str]] = set()
    for name, value in defines.items():
        if name.endswith("_INCLUDED_") or name.startswith(PEER_ONLY_PREFIXES):
            continue
        prefix = next((p for p in prefixes if name.startswith(p)), None)
        if prefix is None:
            continue
        key = (owner[prefix], name[len(prefix):].upper())
        # Any repeat, whatever the values. Two constants that collapse onto one key and happen to
        # agree still leave one of them uncompared, and a declaration the peer has and we do not
        # would then be reported as a match.
        if key in out:
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
        # a C keyword and the two escape it in different places. Only that escape is undone, and only
        # where the path as written is absent, because a leading underscore is also a name a DSDL
        # namespace may simply have -- `isValidNameComponent` allows one -- and that name is ours
        # unchanged. Stripping every leading underscore would rewrite such a name into a different
        # one and compare us against a header that is not ours.
        ours_header = args.ours / relative
        if not ours_header.exists():
            ours_header = args.ours.joinpath(*(unescape_keyword(part) for part in relative.parts))
        if not ours_header.exists():
            failures.append(f"{relative}: generated by the peer and not by us")
            continue
        types += 1

        peer_defines, peer_ambiguous = suffixes(read_defines(peer_header))
        ours_defines, ours_ambiguous = suffixes(read_defines(ours_header))

        # A key that two constants of one type reach is one this comparison cannot speak about, and
        # saying nothing about it is how a disagreement hides. It is a failure of the comparison
        # rather than of either generator, and it is reported as one.
        for owner, key in sorted(peer_ambiguous | ours_ambiguous):
            failures.append(
                f"{relative}: two constants of {owner} reach the key {key}; "
                f"the comparison cannot tell them apart"
            )
        skip = peer_ambiguous | ours_ambiguous

        for key, peer_value in sorted(peer_defines.items()):
            if key in skip:
                continue
            owner, suffix = key
            if key not in ours_defines:
                failures.append(f"{relative}: the peer declares {suffix} on {owner} and we do not")
                continue
            compared += 1
            if normalise(peer_value) != normalise(ours_defines[key]):
                failures.append(
                    f"{relative}: {suffix} on {owner} is {normalise(ours_defines[key])} for us "
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
