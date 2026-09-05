#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#

#===----------------------------------------------------------------------===#
#
# @file
# Pure-Python DSDL runtime primitives with byte-aligned fast-path helpers.
#
# The module preserves the reference runtime API while accelerating aligned copy
# operations commonly emitted by generated serialisers and deserialisers.
#
#===----------------------------------------------------------------------===#

"""Python DSDL runtime primitives with byte-aligned fast-path helpers."""

from __future__ import annotations

import math
import struct
from typing import Union

BytesLike = Union[bytes, bytearray, memoryview]

BACKEND = "pure"


def byte_length_for_bits(total_bits: int) -> int:
    if total_bits <= 0:
        return 0
    return (total_bits + 7) // 8


def _as_readonly_bytes(data: BytesLike) -> bytes:
    if isinstance(data, bytes):
        return data
    if isinstance(data, bytearray):
        return bytes(data)
    if isinstance(data, memoryview):
        return data.tobytes()
    raise TypeError(f"expected bytes-like object, got {type(data)!r}")


def _check_mutable_buffer(buf: bytearray) -> None:
    if not isinstance(buf, bytearray):
        raise TypeError(f"expected bytearray, got {type(buf)!r}")


def _mask_bits(length_bits: int) -> int:
    if length_bits <= 0:
        return 0
    return (1 << length_bits) - 1


def set_bit(buf: bytearray, off_bits: int, value: bool) -> None:
    _check_mutable_buffer(buf)
    byte_index = off_bits // 8
    bit_index = off_bits % 8
    if byte_index < 0 or byte_index >= len(buf):
        raise ValueError("serialisation buffer too small")
    mask = 1 << bit_index
    if value:
        buf[byte_index] = (buf[byte_index] | mask) & 0xFF
    else:
        buf[byte_index] = (buf[byte_index] & (~mask)) & 0xFF


def get_bit(buf: BytesLike, off_bits: int) -> bool:
    source = _as_readonly_bytes(buf)
    byte_index = off_bits // 8
    bit_index = off_bits % 8
    if byte_index < 0 or byte_index >= len(source):
        return False
    return ((source[byte_index] >> bit_index) & 1) == 1


def copy_bits(dst: bytearray, dst_off_bits: int, src: BytesLike, src_off_bits: int, len_bits: int) -> None:
    _check_mutable_buffer(dst)
    source = _as_readonly_bytes(src)
    if len_bits <= 0:
        return

    if dst_off_bits % 8 == 0 and src_off_bits % 8 == 0 and len_bits % 8 == 0:
        dst_start = dst_off_bits // 8
        src_start = src_off_bits // 8
        byte_len = len_bits // 8
        if dst_start < 0 or src_start < 0:
            raise ValueError("negative offsets are not supported")
        if dst_start + byte_len > len(dst) or src_start + byte_len > len(source):
            raise ValueError("serialisation buffer too small")
        dst[dst_start : dst_start + byte_len] = source[src_start : src_start + byte_len]
        return

    for i in range(len_bits):
        set_bit(dst, dst_off_bits + i, get_bit(source, src_off_bits + i))


def extract_bits(src: BytesLike, src_off_bits: int, len_bits: int) -> bytes:
    source = _as_readonly_bytes(src)
    if src_off_bits % 8 == 0 and len_bits % 8 == 0:
        src_start = src_off_bits // 8
        byte_len = len_bits // 8
        if src_start < 0:
            raise ValueError("negative offsets are not supported")
        if src_start + byte_len > len(source):
            raise ValueError("serialisation buffer too small")
        return source[src_start : src_start + byte_len]

    out = bytearray(byte_length_for_bits(len_bits))
    copy_bits(out, 0, source, src_off_bits, len_bits)
    return bytes(out)


def write_unsigned(buf: bytearray, off_bits: int, len_bits: int, value: int, saturating: bool) -> None:
    _check_mutable_buffer(buf)
    if len_bits <= 0:
        return
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


def write_signed(buf: bytearray, off_bits: int, len_bits: int, value: int, saturating: bool) -> None:
    _check_mutable_buffer(buf)
    if len_bits <= 0:
        return
    value_int = int(value)
    if saturating:
        min_value = -(1 << (len_bits - 1))
        max_value = (1 << (len_bits - 1)) - 1
        if value_int < min_value:
            value_int = min_value
        elif value_int > max_value:
            value_int = max_value
    write_unsigned(buf, off_bits, len_bits, value_int, False)


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


def write_float(buf: bytearray, off_bits: int, len_bits: int, value: float) -> None:
    if math.isnan(value):
        value = float("nan")
    if len_bits == 16:
        write_unsigned(buf, off_bits, len_bits, _float16_to_bits(value), False)
        return
    if len_bits == 32:
        write_unsigned(buf, off_bits, len_bits, _float32_to_bits(value), False)
        return
    if len_bits == 64:
        write_unsigned(buf, off_bits, len_bits, _float64_to_bits(value), False)
        return
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
