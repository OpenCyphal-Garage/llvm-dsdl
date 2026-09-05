#!/usr/bin/env python3
"""Checks that the symbols a schema names are the ones the generated header declares.

An object lowering emits its serialisers under the names the plan carries, and links against
headers emitted from the same model by different code. If the two disagree the object defines
one symbol and every caller looks for another, which is a link error at best and, where a
weak or duplicate declaration absorbs it, the wrong function at worst.

Lowering cannot know these names: they move with the backend's naming options, so it stamps
the unversioned spelling and a backend rewrites it. This checks that the rewrite happened and
landed on what the header says, which is the only place the two are made to agree.

Four things are held against the header:

  * every `c_serialize_symbol` and `c_deserialize_symbol` is a function it declares;
  * every plan's `c_type_name` is a type it defines;
  * every `composite_c_type_name` -- the callee a nested type is serialised through -- names a
    type it defines, since that is what resolves the call across translation units;
  * the schema names at least as many types as the corpus has headers, so a run that stamped
    nothing cannot pass by having nothing to disagree about.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SYMBOL = re.compile(r'c_(?:de)?serialize_symbol = "([^"]+)"')
# Only a plan's type name is a struct. A service's schema names the base its sections suffix,
# and nothing is defined under that.
PLAN_TYPE_NAME = re.compile(r'dsdl\.serialization_plan[^\n]*?\bc_type_name = "([^"]+)"')
COMPOSITE = re.compile(r'composite_c_type_name = "([^"]+)"')

# `int8_t name__serialize_(` -- the declaration the header publishes.
DECLARED_FN = re.compile(r'\b([A-Za-z_]\w*__(?:de)?serialize_)\s*\(')
# `} name;` closing a typedef, which is how every generated type is introduced. A deprecated
# type carries its attribute after the name, where GCC needs it to warn on use.
DEFINED_TYPE = re.compile(r'^\}\s*([A-Za-z_]\w*)\b[^;]*;', re.M)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--c-root", required=True, type=Path)
    parser.add_argument("--mlir", required=True, type=Path)
    args = parser.parse_args()

    schema = args.mlir.read_text(encoding="utf-8")
    declared_fns: set[str] = set()
    defined_types: set[str] = set()
    headers = 0
    for header in args.c_root.rglob("*.h"):
        text = header.read_text(encoding="utf-8", errors="replace")
        declared_fns.update(DECLARED_FN.findall(text))
        defined_types.update(DEFINED_TYPE.findall(text))
        headers += 1

    symbols = set(SYMBOL.findall(schema))
    types = set(PLAN_TYPE_NAME.findall(schema))
    composites = set(COMPOSITE.findall(schema))

    failures: list[str] = []
    for symbol in sorted(symbols - declared_fns):
        failures.append(f"schema names {symbol}, which no header declares")
    for name in sorted(types - defined_types):
        failures.append(f"schema names the type {name}, which no header defines")
    for name in sorted(composites - defined_types):
        failures.append(f"a nested type is serialised through {name}, which no header defines")

    if not symbols or not types:
        failures.append("the schema names no symbols or no types, so it asserts nothing")
    if len(types) < headers:
        failures.append(f"{headers} header(s) were generated but the schema names only "
                        f"{len(types)} type(s)")

    if failures:
        print("schema symbol parity failed: the names an object would emit are not the names "
              "the header declares")
        for failure in failures[:12]:
            print(f"    {failure}")
        if len(failures) > 12:
            print(f"    ... and {len(failures) - 12} more")
        return 1

    print(f"schema symbol parity: {len(symbols)} symbol(s), {len(types)} type name(s) and "
          f"{len(composites)} nested callee(s) match what {headers} header(s) declare")
    return 0


if __name__ == "__main__":
    sys.exit(main())
