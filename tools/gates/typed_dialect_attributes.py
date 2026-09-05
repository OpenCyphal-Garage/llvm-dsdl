#!/usr/bin/env python3
"""Every fact a dialect op carries is a declared attribute, read through its accessor.

`DSDLOps.td` declares the attributes each `dsdl` op carries, and ODS generates a typed accessor
for every one of them. A consumer that reads one by its name string is a second copy of that
contract, which the verifier does not hold and a rename does not reach; a consumer that tells
ops apart by their name string is the same thing one level up.

This reads the declared names out of the dialect definition, so an attribute added there is
covered the moment it is declared.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

DIALECT_OPS = Path("include/llvmdsdl/IR/DSDLOps.td")
SOURCE_ROOTS = ("include", "lib", "tools")
EXCLUDED_PARTS = ("build", "submodules", "site-packages")

# `op->getAttrOfType<T>("name")`, `op.hasAttr("name")`, `op->setAttr("name", ...)` and the rest of
# the by-name surface. Dotted names such as `llvmdsdl.layout_only` are discardable attributes by
# design and are not the dialect's to declare.
BY_NAME = re.compile(r"\b(?:getAttrOfType<[^>]*>|hasAttr|setAttr|getAttr|removeAttr|getNamedAttr|"
                     r"getDiscardableAttr|setDiscardableAttr|removeDiscardableAttr)\s*\(\s*\"([a-z_]+)\"")
# `op.getName().getStringRef() == "dsdl.io"` where `isa<IOOp>` says the same thing and is checked.
OP_BY_NAME = re.compile(r"getStringRef\(\)\s*[!=]=\s*\"dsdl\.[a-z_]+\"")


def declared_attributes(dialect_ops: Path) -> set[str]:
    text = dialect_ops.read_text(encoding="utf-8")
    names = {"sym_name"}
    for block in re.findall(r"let arguments = \(ins(.*?)\);", text, flags=re.S):
        names.update(re.findall(r":\$([a-z_]+)", block))
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo-root", required=True, type=Path)
    args = parser.parse_args()

    names = declared_attributes(args.repo_root / DIALECT_OPS)
    violations: list[str] = []
    checked = 0
    for root in SOURCE_ROOTS:
        for path in sorted((args.repo_root / root).rglob("*")):
            if path.suffix not in (".cpp", ".h") or any(part in EXCLUDED_PARTS for part in path.parts):
                continue
            checked += 1
            relative = path.relative_to(args.repo_root)
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                for match in BY_NAME.finditer(line):
                    if match.group(1) in names:
                        violations.append(f"{relative}:{number}: attribute '{match.group(1)}' read by name; "
                                          f"the op declares it, so use its accessor")
                if OP_BY_NAME.search(line):
                    violations.append(f"{relative}:{number}: dialect op matched by its name string; use isa<> or dyn_cast<>")

    if violations:
        print(f"typed dialect attributes gate: {len(violations)} violation(s) in {checked} file(s):")
        for violation in violations:
            print(f"  {violation}")
        return 1
    print(f"typed dialect attributes gate: {len(names)} declared attribute(s), no by-name access in {checked} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
