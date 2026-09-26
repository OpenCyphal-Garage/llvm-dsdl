#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Hold each emitter's queries of the IR around the op it spells to an inventory that may only empty.

An emitter spells the operation it is handed. When it looks past that operation -- at a value's
uses, at the operation that defined an operand, at every return of the function, at the block an
operation sits in -- it is deciding what the body means, and each emitter that needs the answer
decides it again. `CLEAN_CODE.md`'s *Discipline* found four such decisions answered in the emitters:
whether a body can fail, a nested call's adaptation to a callee that answers its size or an error, a
bool array's run of bits, and whether a size or a parameter is read. Phase 3 states them once, in the
IR or the shared translator.

These are the queries counted, per emitter:

* `use_empty`, `getUsers`, `getUses`, `hasOneUse`: whether, or how, a value is observed;
* `getDefiningOp`, `matchPattern`: recognising the shape of what reached an operand;
* `walk`: deriving a property from every operation of a region;
* `getBlock`: where an operation sits.

Symbol lookup through the module, a type test on the operation being spelt and an attribute read are
not queries of this kind.

The inventory in `test/integration/emitter-ir-queries.json` is the tree as it stands, and the
comparison is exact. A count that rises is a new decision in an emitter, which belongs in the IR
instead. A count that falls is a decision moved out, which is the point, and is retaken with --update
so the inventory stays the tree's. Phase 3 is done when the inventory is empty.

    python3 tools/check_emitter_ir_queries.py
    python3 tools/check_emitter_ir_queries.py --update
    python3 tools/check_emitter_ir_queries.py --self-test
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

from check_language_classification import STRING_LITERAL, strip_comments

REPO_ROOT = Path(__file__).resolve().parent.parent
EMITTERS = Path("lib") / "CodeGen" / "emitter"
INVENTORY = Path("test") / "integration" / "emitter-ir-queries.json"

QUERIES = {
    "use_empty": re.compile(r"\buse_empty\s*\("),
    "getUsers": re.compile(r"\bgetUsers\s*\("),
    "getUses": re.compile(r"\bgetUses\s*\("),
    "hasOneUse": re.compile(r"\bhasOneUse\s*\("),
    "getDefiningOp": re.compile(r"\bgetDefiningOp\b"),
    "matchPattern": re.compile(r"\bmatchPattern\s*\("),
    "walk": re.compile(r"(?:\.|->)walk\s*\("),
    "getBlock": re.compile(r"(?:\.|->)getBlock\s*\(\s*\)"),
}


def count(root: Path) -> dict[str, dict[str, int]]:
    """Count each emitter's queries under @p root, omitting an emitter that makes none."""
    counts: dict[str, dict[str, int]] = {}
    for path in sorted((root / EMITTERS).glob("*.cpp")):
        found: dict[str, int] = {}
        for line in strip_comments(path.read_text(errors="replace")):
            # A query spelt inside a string literal is text the emitter writes, not a query.
            line = STRING_LITERAL.sub('""', line)
            for name, pattern in QUERIES.items():
                hits = len(pattern.findall(line))
                if hits:
                    found[name] = found.get(name, 0) + hits
        if found:
            counts[path.name] = dict(sorted(found.items()))
    return counts


def differences(counts: dict[str, dict[str, int]], recorded: dict[str, dict[str, int]]) -> tuple[list[str], list[str]]:
    """Return the queries @p counts has beyond @p recorded, and those it has fewer of."""
    rises, falls = [], []
    for emitter in sorted(set(counts) | set(recorded)):
        now, was = counts.get(emitter, {}), recorded.get(emitter, {})
        for query in sorted(set(now) | set(was)):
            a, b = now.get(query, 0), was.get(query, 0)
            if a > b:
                rises.append(f"{emitter}: {query} {a}, inventory {b}")
            elif a < b:
                falls.append(f"{emitter}: {query} {a}, inventory {b}")
    return rises, falls


def self_test() -> int:
    """Hold the count to sources it must count and sources it must not."""
    cases = {
        "fn.getArgument(1).use_empty() && x.getDefiningOp<A>()": {"use_empty": 1, "getDefiningOp": 1},
        "fn.walk([&](Op op) {}); fn->walk(f); w->getBlock();": {"walk": 2, "getBlock": 1},
        "for (auto* u : v.getUsers()) if (u->hasOneUse()) {}": {"getUsers": 1, "hasOneUse": 1},
        "mlir::matchPattern(v, mlir::m_Zero())": {"matchPattern": 1},
        "// fn.walk() and v.use_empty() explain, and decide nothing": {},
        'w.line("x.use_empty()");': {},
        "auto m = fn->getParentOfType<mlir::ModuleOp>(); if (mlir::isa<A>(op)) {}": {},
        "fn->hasAttr(\"llvmdsdl.infallible\")": {},
    }
    failures = []
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        (root / EMITTERS).mkdir(parents=True)
        for text, expected in cases.items():
            (root / EMITTERS / "X.cpp").write_text(text + "\n")
            got = count(root).get("X.cpp", {})
            if got != expected:
                failures.append(f"{text!r}: expected {expected}, counted {got}")
        (root / EMITTERS / "X.cpp").write_text("v.use_empty(); v.use_empty();\n")
        rises, falls = differences(count(root), {"X.cpp": {"use_empty": 1}})
        if not rises or falls:
            failures.append("a rise over the inventory is not reported as one")
        rises, falls = differences(count(root), {"X.cpp": {"use_empty": 3}})
        if rises or not falls:
            failures.append("a fall under the inventory is not reported as one")
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print(f"self-test: {len(cases)} sources counted as expected, and both directions reported")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="the repository to check")
    parser.add_argument("--update", action="store_true", help="rewrite the inventory from the tree")
    parser.add_argument("--self-test", action="store_true", help="check the check rather than the tree")
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()

    counts = count(arguments.root)
    total = sum(sum(queries.values()) for queries in counts.values())
    inventory = arguments.root / INVENTORY
    if arguments.update:
        inventory.write_text(json.dumps(counts, indent=2) + "\n")
        print(f"inventory rewritten: {inventory} ({total} queries)")
        return 0

    rises, falls = differences(counts, json.loads(inventory.read_text()))
    if not rises and not falls:
        print(f"the emitters make the {total} queries the inventory records")
        return 0
    if rises:
        print("an emitter decides what a body means, beyond the inventory:", file=sys.stderr)
        for rise in rises:
            print(f"  {rise}", file=sys.stderr)
        print("State the answer in the IR or the shared translator, and read it there.", file=sys.stderr)
    if falls:
        print("a decision left an emitter, and the inventory still counts it:", file=sys.stderr)
        for fall in falls:
            print(f"  {fall}", file=sys.stderr)
        print("Retake the inventory with --update.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
