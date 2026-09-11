#!/usr/bin/env python3
#===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===//
"""Check that a union serialises to the size of its selected arm and round-trips."""
from __future__ import annotations
import importlib
import sys


def main(out_dir: str) -> int:
    sys.path.insert(0, out_dir)
    module = importlib.import_module("union_padding.backend_contract.contract.choice_1_0")
    choice = module.Choice

    small = choice()
    small._tag = 0
    small.small = 0x55
    encoded = small.serialize()
    if encoded != bytes([0x00, 0x55]):
        print(f"small arm: expected 0055, got {encoded.hex()}")
        return 1

    wide = choice()
    wide._tag = 1
    wide.wide = 0x1234
    encoded = wide.serialize()
    if encoded != bytes([0x01, 0x34, 0x12]):
        print(f"wide arm: expected 013412, got {encoded.hex()}")
        return 1

    for original in (small, wide):
        decoded = choice.deserialize(original.serialize())
        if decoded != original:
            print(f"round trip changed the object: {original} -> {decoded}")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
