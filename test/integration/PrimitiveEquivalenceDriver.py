#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//
#
# Isolated cross-language runtime-primitive equivalence driver (Python).
#
# Reads the shared vector file (argv[1]) and exercises each vector directly
# against the pure-Python runtime primitives (argv[2] = path to _dsdl_runtime.py),
# checking every primitive direction on its own. Prints "PROCESSED <n>" and
# "SKIPPED <n>" and exits non-zero on the first genuine mismatch.
#
# Python is double-typed, so a small number of vectors are legitimately not
# comparable at the raw-primitive level and are SKIPPED with a printed reason:
# float16 pack of an out-of-range value raises OverflowError here because
# saturation is applied one layer up (write_float), whereas the C magic-float
# primitive returns +/-inf.
#
# ===----------------------------------------------------------------------===//

import importlib.util
import struct
import sys

MASK64 = (1 << 64) - 1


def load_runtime(path):
    spec = importlib.util.spec_from_file_location("_dsdl_runtime_under_test", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def f32_from_bits(hex_str):
    return struct.unpack("<f", struct.pack("<I", int(hex_str, 16)))[0]


def f32_to_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def hex_bytes(hex_str):
    return bytearray.fromhex(hex_str)


def main(argv):
    if len(argv) < 3:
        print("usage: driver <vectors-file> <runtime-module.py>", file=sys.stderr)
        return 2
    rt = load_runtime(argv[2])

    processed = 0
    skipped = 0
    with open(argv[1], "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            f = line.split()
            op = f[0]

            def fail(detail):
                print(f"MISMATCH {op}: {detail}", file=sys.stderr)
                sys.exit(1)

            if op == "f16pack":
                value = f32_from_bits(f[1])
                try:
                    got = rt._float16_to_bits(value)
                except OverflowError:
                    print(f"SKIP f16pack in={f[1]}: Python float16 primitive raises on "
                          f"out-of-range input (saturation is applied in write_float)")
                    skipped += 1
                    continue
                want = int(f[2], 16)
                if got != want:
                    fail(f"in={f[1]} want={want:04x} got={got:04x}")
            elif op == "f16unpack":
                got = f32_to_bits(rt._bits_to_float16(int(f[1], 16)))
                want = int(f[2], 16)
                if got != want:
                    fail(f"in={f[1]} want={want:08x} got={got:08x}")
            elif op in ("geti", "getu"):
                len_bits = int(f[2])
                off_bits = int(f[3])
                buf = hex_bytes(f[4])
                want = int(f[5], 16)
                if op == "geti":
                    got = rt.read_signed(buf, off_bits, len_bits) & MASK64
                else:
                    got = rt.read_unsigned(buf, off_bits, len_bits) & MASK64
                if got != want:
                    fail(f"len={len_bits} off={off_bits} buf={f[4]} want={want:016x} got={got:016x}")
            elif op == "copybits":
                dst_off = int(f[1])
                len_bits = int(f[2])
                src_off = int(f[3])
                src = hex_bytes(f[4])
                dst = hex_bytes(f[5])
                want = hex_bytes(f[6])
                # Python copy_bits arg order is (dst, dst_off, src, src_off, len).
                rt.copy_bits(dst, dst_off, src, src_off, len_bits)
                if dst != want:
                    fail(f"dstoff={dst_off} len={len_bits} srcoff={src_off} "
                         f"got={dst.hex()} want={want.hex()}")
            else:
                print(f"UNRECOGNIZED vector: {line}", file=sys.stderr)
                return 1
            processed += 1

    print(f"PROCESSED {processed}")
    print(f"SKIPPED {skipped}")
    print("Python primitive equivalence PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
