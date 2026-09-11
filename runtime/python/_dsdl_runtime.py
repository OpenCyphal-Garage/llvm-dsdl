#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#

#===----------------------------------------------------------------------===#
#
# @file
# Pure-Python DSDL runtime primitives used by generated serialisers/deserializers.
#
# The module provides reference bit, integer, and floating-point helpers that
# match the runtime contract used by generated Python packages.
#
#===----------------------------------------------------------------------===#

"""Python DSDL runtime primitives used by generated serialisers/deserializers."""

from __future__ import annotations

import math
import struct
from typing import Union

BytesLike = Union[bytes, bytearray, memoryview]
WritableBytes = Union[bytearray, memoryview]

BACKEND = "pure"


def byte_length_for_bits(total_bits: int) -> int:
    if total_bits <= 0:
        return 0
    return (total_bits + 7) // 8


def _byte_view(view: memoryview) -> memoryview:
    # A view is addressed by byte, whatever the format of the buffer it was taken from; the
    # accelerator reads the same buffer as bytes.
    if view.format == "B" and view.ndim == 1 and view.c_contiguous:
        return view
    return view.cast("B")


def _as_readonly_bytes(data: BytesLike) -> BytesLike:
    # A view is read in place: a body hands its nested bodies views into one buffer.
    if isinstance(data, (bytes, bytearray)):
        return data
    if isinstance(data, memoryview):
        return _byte_view(data)
    raise TypeError(f"expected bytes-like object, got {type(data)!r}")


def _as_writable_bytes(buf: WritableBytes) -> WritableBytes:
    if isinstance(buf, bytearray):
        return buf
    if isinstance(buf, memoryview) and not buf.readonly:
        return _byte_view(buf)
    raise TypeError(f"expected bytearray or writable memoryview, got {type(buf)!r}")


def _mask_bits(length_bits: int) -> int:
    if length_bits <= 0:
        return 0
    return (1 << length_bits) - 1


def set_bit(buf: WritableBytes, off_bits: int, value: bool) -> int:
    buf = _as_writable_bytes(buf)
    byte_index = off_bits // 8
    bit_index = off_bits % 8
    if byte_index < 0 or byte_index >= len(buf):
        raise ValueError("serialisation buffer too small")
    mask = 1 << bit_index
    if value:
        buf[byte_index] = (buf[byte_index] | mask) & 0xFF
    else:
        buf[byte_index] = (buf[byte_index] & (~mask)) & 0xFF
    return 0


def get_bit(buf: BytesLike, off_bits: int) -> bool:
    source = _as_readonly_bytes(buf)
    byte_index = off_bits // 8
    bit_index = off_bits % 8
    if byte_index < 0 or byte_index >= len(source):
        return False
    return ((source[byte_index] >> bit_index) & 1) == 1


def copy_bits(dst: WritableBytes, dst_off_bits: int, src: BytesLike, src_off_bits: int, len_bits: int) -> None:
    dst = _as_writable_bytes(dst)
    source = _as_readonly_bytes(src)
    if len_bits <= 0:
        return

    for i in range(len_bits):
        set_bit(dst, dst_off_bits + i, get_bit(source, src_off_bits + i))


def extract_bits(src: BytesLike, src_off_bits: int, len_bits: int) -> bytes:
    out = bytearray(byte_length_for_bits(len_bits))
    copy_bits(out, 0, src, src_off_bits, len_bits)
    return bytes(out)


def write_unsigned(buf: WritableBytes, off_bits: int, len_bits: int, value: int, saturating: bool) -> int:
    buf = _as_writable_bytes(buf)
    if len_bits <= 0:
        return 0
    value_int = int(value)
    max_value = _mask_bits(len_bits)
    if saturating:
        if value_int < 0:
            value_int = 0
        elif value_int > max_value:
            value_int = max_value
    else:
        value_int &= max_value
    for i in range(len_bits):
        set_bit(buf, off_bits + i, ((value_int >> i) & 1) == 1)
    return 0


def write_signed(buf: WritableBytes, off_bits: int, len_bits: int, value: int, saturating: bool) -> int:
    buf = _as_writable_bytes(buf)
    if len_bits <= 0:
        return 0
    value_int = int(value)
    if saturating:
        min_value = -(1 << (len_bits - 1))
        max_value = (1 << (len_bits - 1)) - 1
        if value_int < min_value:
            value_int = min_value
        elif value_int > max_value:
            value_int = max_value
    return write_unsigned(buf, off_bits, len_bits, value_int, False)


def read_unsigned(buf: BytesLike, off_bits: int, len_bits: int) -> int:
    source = _as_readonly_bytes(buf)
    if len_bits <= 0:
        return 0
    out = 0
    for i in range(len_bits):
        if get_bit(source, off_bits + i):
            out |= 1 << i
    return out


def read_signed(buf: BytesLike, off_bits: int, len_bits: int) -> int:
    if len_bits <= 0:
        return 0
    raw = read_unsigned(buf, off_bits, len_bits)
    sign_bit = 1 << (len_bits - 1)
    if raw & sign_bit:
        return raw - (1 << len_bits)
    return raw


def _float16_to_bits(value: float) -> int:
    return struct.unpack("<H", struct.pack("<e", float(value)))[0]


def _bits_to_float16(bits: int) -> float:
    return struct.unpack("<e", struct.pack("<H", bits & 0xFFFF))[0]


def _float32_to_bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", float(value)))[0]


def _bits_to_float32(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def _float64_to_bits(value: float) -> int:
    return struct.unpack("<Q", struct.pack("<d", float(value)))[0]


def _bits_to_float64(bits: int) -> float:
    return struct.unpack("<d", struct.pack("<Q", bits & 0xFFFFFFFFFFFFFFFF))[0]


def write_float(buf: WritableBytes, off_bits: int, len_bits: int, value: float) -> int:
    if math.isnan(value):
        value = float("nan")
    if len_bits == 16:
        return write_unsigned(buf, off_bits, len_bits, _float16_to_bits(value), False)
    if len_bits == 32:
        return write_unsigned(buf, off_bits, len_bits, _float32_to_bits(value), False)
    if len_bits == 64:
        return write_unsigned(buf, off_bits, len_bits, _float64_to_bits(value), False)
    raise ValueError(f"unsupported float bit length {len_bits}")


def read_float(buf: BytesLike, off_bits: int, len_bits: int) -> float:
    raw = read_unsigned(buf, off_bits, len_bits)
    if len_bits == 16:
        return _bits_to_float16(raw)
    if len_bits == 32:
        return _bits_to_float32(raw)
    if len_bits == 64:
        return _bits_to_float64(raw)
    raise ValueError(f"unsupported float bit length {len_bits}")
