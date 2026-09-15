#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//
#
# Array-length prefixes decoded by the generated Python: above the capacity, beyond the index
# width, and at the capacity. One line per case and a summary line the lane parses. A list holds
# at most sys.maxsize elements, so the cases beyond a 32-bit index are rejected on a 32-bit
# interpreter and reported skipped on a wider one.
#
# Arguments: the generated output directory, the package name, and the runtime backend to load
# (pure or accel). The driver fails when the loader settles on a different backend.
#
# ===----------------------------------------------------------------------===//

from __future__ import annotations

import importlib
import os
import sys
from typing import Callable

# The code a body answers with for an array length outside the declared capacity.
BAD_ARRAY_LENGTH = -10

passed = 0
skipped = 0
failed = 0


def outcome(verdict: str, name: str, prefix: int, detail: str = "") -> None:
    global passed, skipped, failed
    if verdict == "PASS":
        passed += 1
    elif verdict == "SKIP":
        skipped += 1
    else:
        failed += 1
    if not detail:
        print(f"{verdict} {name} prefix={prefix}")
    elif verdict == "FAIL":
        print(f"{verdict} {name} prefix={prefix}: {detail}")
    else:
        print(f"{verdict} {name} prefix={prefix} {detail}")


def with_prefix(prefix: int, prefix_bytes: int, payload_bytes: int) -> bytearray:
    """A buffer opening with a little-endian prefix followed by zero payload bytes."""
    return bytearray(prefix.to_bytes(prefix_bytes, "little")) + bytearray(payload_bytes)


def expect_rejected(name: str, prefix: int, decode: Callable[[], int], length_after: Callable[[], int]) -> None:
    """A decode that has to fail with a bad array length and leave the array empty."""
    try:
        rc = decode()
    except Exception as error:  # noqa: BLE001 - any exception is the failure being reported.
        outcome("FAIL", name, prefix, f"raised {error!r}")
        return
    if rc != BAD_ARRAY_LENGTH:
        outcome("FAIL", name, prefix, f"rc = {rc}, want {BAD_ARRAY_LENGTH}")
        return
    if length_after() != 0:
        outcome("FAIL", name, prefix, f"array holds {length_after()} elements after rejection")
        return
    outcome("PASS", name, prefix)


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print("usage: driver <generated-out-dir> <package> <pure|accel>", file=sys.stderr)
        return 2
    out_dir, package, mode = argv[1], argv[2], argv[3]
    os.environ["LLVMDSDL_PY_RUNTIME_MODE"] = mode
    sys.path.insert(0, out_dir)
    loader = importlib.import_module(f"{package}._runtime_loader")
    if loader.BACKEND != mode:
        print(f"FAIL runtime backend is {loader.BACKEND}, want {mode}")
        return 1
    print(f"BACKEND {loader.BACKEND}")
    prefix32 = importlib.import_module(f"{package}.prefixguard.prefix32_1_0").Prefix32@V1_0@
    prefix64 = importlib.import_module(f"{package}.prefixguard.prefix64_1_0").Prefix64@V1_0@

    for prefix in (65537, 1 << 31, (1 << 32) - 1):
        obj = prefix32()
        buffer = with_prefix(prefix, 4, 0)
        expect_rejected(
            "prefix32_rejects_above_capacity",
            prefix,
            lambda: obj._deserialize_from(memoryview(buffer)),
            lambda: len(obj.payload),
        )

    capacity = 65536
    obj = prefix32()
    buffer = with_prefix(capacity, 4, capacity)
    try:
        rc = obj._deserialize_from(memoryview(buffer))
    except Exception as error:  # noqa: BLE001
        outcome("FAIL", "prefix32_accepts_capacity", capacity, f"raised {error!r}")
    else:
        if rc != len(buffer):
            outcome("FAIL", "prefix32_accepts_capacity", capacity, f"rc = {rc}, want {len(buffer)} bytes consumed")
        elif len(obj.payload) != capacity:
            outcome("FAIL", "prefix32_accepts_capacity", capacity, f"payload holds {len(obj.payload)} elements, want {capacity}")
        else:
            outcome("PASS", "prefix32_accepts_capacity", capacity)

    for prefix in ((1 << 33) + 1, (1 << 33) + 3, 1 << 63, (1 << 64) - 1):
        obj = prefix64()
        buffer = with_prefix(prefix, 8, 0)
        expect_rejected(
            "prefix64_rejects_above_capacity",
            prefix,
            lambda: obj._deserialize_from(memoryview(buffer)),
            lambda: len(obj.flags),
        )

    for prefix in (1 << 32, (1 << 32) + 3, 1 << 33):
        if sys.maxsize >= (1 << 33):
            outcome("SKIP", "prefix64_rejects_beyond_index", prefix, "a list holds every length Prefix64 allows")
            continue
        obj = prefix64()
        buffer = with_prefix(prefix, 8, 0)
        expect_rejected(
            "prefix64_rejects_beyond_index",
            prefix,
            lambda: obj._deserialize_from(memoryview(buffer)),
            lambda: len(obj.flags),
        )

    obj = prefix64()
    buffer = with_prefix(3, 8, 1)
    buffer[8] = 0x05
    want = [True, False, True]
    try:
        rc = obj._deserialize_from(memoryview(buffer))
    except Exception as error:  # noqa: BLE001
        outcome("FAIL", "prefix64_accepts_small_length", 3, f"raised {error!r}")
    else:
        if rc != len(buffer):
            outcome("FAIL", "prefix64_accepts_small_length", 3, f"rc = {rc}, want {len(buffer)} bytes consumed")
        elif list(obj.flags) != want:
            outcome("FAIL", "prefix64_accepts_small_length", 3, f"flags = {list(obj.flags)!r}, want {want!r}")
        else:
            outcome("PASS", "prefix64_accepts_small_length", 3)

    status = "PASS" if failed == 0 else "FAIL"
    print(
        f"{status} python-array-length-prefix-guard cases={passed + skipped + failed} "
        f"passed={passed} skipped={skipped} failed={failed}"
    )
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
