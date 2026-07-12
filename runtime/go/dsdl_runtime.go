//===----------------------------------------------------------------------===//
///
/// @file
/// Portable bit-level runtime primitives for generated Go DSDL bindings.
///
/// This module provides integer, floating-point, and bit-copy helpers shared by
/// generated serializers and deserializers.
///
//===----------------------------------------------------------------------===//

// Package dsdlruntime provides the portable bit-level runtime used by generated
// DSDL Go bindings.
package dsdlruntime

import "math"

const (
	// DSDL_RUNTIME_SUCCESS indicates successful runtime execution.
	DSDL_RUNTIME_SUCCESS int8 = 0
	// DSDL_RUNTIME_ERROR_INVALID_ARGUMENT indicates invalid API arguments.
	DSDL_RUNTIME_ERROR_INVALID_ARGUMENT int8 = 2
	// DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL indicates that the
	// destination buffer cannot hold the requested serialized bits.
	DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL int8 = 3
	// DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH indicates malformed
	// array-length representation in serialized data.
	DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH int8 = 10
	// DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG indicates malformed union
	// discriminator representation in serialized data.
	DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG int8 = 11
	// DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER indicates malformed
	// delimiter header representation in serialized data.
	DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER int8 = 12
)

// ChooseMin returns the smaller of a and b.
func ChooseMin(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// SaturateFragmentBits returns the number of bits that can be consumed from a
// fragment without running past the end of a byte buffer.
func SaturateFragmentBits(bufferSizeBytes, fragmentOffsetBits, fragmentLengthBits int) int {
	sizeBits := bufferSizeBytes * 8
	tailBits := sizeBits - ChooseMin(sizeBits, fragmentOffsetBits)
	return ChooseMin(fragmentLengthBits, tailBits)
}

// CopyBits copies a bit fragment from src into dst using DSDL bit ordering.
// Offsets and length are expressed in bits.
func CopyBits(dst []byte, dstOffsetBits, lengthBits int, src []byte, srcOffsetBits int) {
	for i := 0; i < lengthBits; i++ {
		srcBitIndex := srcOffsetBits + i
		if (srcBitIndex / 8) >= len(src) {
			break
		}
		dstBitIndex := dstOffsetBits + i
		if (dstBitIndex / 8) >= len(dst) {
			break
		}
		bit := ((src[srcBitIndex/8] >> (srcBitIndex % 8)) & 1) != 0
		if bit {
			dst[dstBitIndex/8] |= uint8(1) << uint(dstBitIndex%8)
		} else {
			dst[dstBitIndex/8] &^= uint8(1) << uint(dstBitIndex%8)
		}
	}
}

// GetBits copies a bit fragment from buf into output and implicitly zero-extends
// missing bits when the request exceeds the source buffer bounds.
func GetBits(output []byte, buf []byte, offBits, lenBits int) {
	satBits := SaturateFragmentBits(len(buf), offBits, lenBits)
	outLen := (lenBits + 7) / 8
	if outLen > len(output) {
		outLen = len(output)
	}
	for i := 0; i < outLen; i++ {
		output[i] = 0
	}
	CopyBits(output, 0, satBits, buf, offBits)
}

// SetBit serializes a single boolean bit at offBits in buf.
// It returns DSDL_RUNTIME_SUCCESS on success or a negative runtime error code.
func SetBit(buf []byte, offBits int, value bool) int8 {
	if len(buf)*8 <= offBits {
		return -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL
	}
	tmp := byte(0)
	if value {
		tmp = 1
	}
	CopyBits(buf, offBits, 1, []byte{tmp}, 0)
	return DSDL_RUNTIME_SUCCESS
}

// SetUxx serializes an unsigned integer fragment of lenBits width at offBits.
// It returns DSDL_RUNTIME_SUCCESS on success or a negative runtime error code.
func SetUxx(buf []byte, offBits int, value uint64, lenBits uint8) int8 {
	if len(buf)*8 < offBits+int(lenBits) {
		return -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL
	}
	saturatedLenBits := ChooseMin(int(lenBits), 64)
	tmp := [8]byte{
		byte(value),
		byte(value >> 8),
		byte(value >> 16),
		byte(value >> 24),
		byte(value >> 32),
		byte(value >> 40),
		byte(value >> 48),
		byte(value >> 56),
	}
	CopyBits(buf, offBits, saturatedLenBits, tmp[:], 0)
	return DSDL_RUNTIME_SUCCESS
}

// SetIxx serializes a signed integer fragment of lenBits width at offBits.
// It returns DSDL_RUNTIME_SUCCESS on success or a negative runtime error code.
func SetIxx(buf []byte, offBits int, value int64, lenBits uint8) int8 {
	return SetUxx(buf, offBits, uint64(value), lenBits)
}

// GetBit deserializes one bit from buf at offBits.
func GetBit(buf []byte, offBits int) bool {
	return GetU8(buf, offBits, 1) == 1
}

// GetU8 deserializes up to 8 bits as an unsigned integer.
func GetU8(buf []byte, offBits int, lenBits uint8) uint8 {
	bits := SaturateFragmentBits(len(buf), offBits, ChooseMin(int(lenBits), 8))
	var out [1]byte
	CopyBits(out[:], 0, bits, buf, offBits)
	return out[0]
}

// GetU16 deserializes up to 16 bits as an unsigned integer.
func GetU16(buf []byte, offBits int, lenBits uint8) uint16 {
	bits := SaturateFragmentBits(len(buf), offBits, ChooseMin(int(lenBits), 16))
	var out [2]byte
	CopyBits(out[:], 0, bits, buf, offBits)
	return uint16(out[0]) | (uint16(out[1]) << 8)
}

// GetU32 deserializes up to 32 bits as an unsigned integer.
func GetU32(buf []byte, offBits int, lenBits uint8) uint32 {
	bits := SaturateFragmentBits(len(buf), offBits, ChooseMin(int(lenBits), 32))
	var out [4]byte
	CopyBits(out[:], 0, bits, buf, offBits)
	return uint32(out[0]) |
		(uint32(out[1]) << 8) |
		(uint32(out[2]) << 16) |
		(uint32(out[3]) << 24)
}

// GetU64 deserializes up to 64 bits as an unsigned integer.
func GetU64(buf []byte, offBits int, lenBits uint8) uint64 {
	bits := SaturateFragmentBits(len(buf), offBits, ChooseMin(int(lenBits), 64))
	var out [8]byte
	CopyBits(out[:], 0, bits, buf, offBits)
	return uint64(out[0]) |
		(uint64(out[1]) << 8) |
		(uint64(out[2]) << 16) |
		(uint64(out[3]) << 24) |
		(uint64(out[4]) << 32) |
		(uint64(out[5]) << 40) |
		(uint64(out[6]) << 48) |
		(uint64(out[7]) << 56)
}

// signExtendU64 sign-extends value assuming sat significant bits.
func signExtendU64(value uint64, sat uint8) uint64 {
	if sat == 0 || sat >= 64 {
		return value
	}
	signBit := uint64(1) << sat
	signBit >>= 1
	if (value & signBit) == 0 {
		return value
	}
	return value | (^uint64(0) << sat)
}

// GetI8 deserializes up to 8 bits as a signed integer with sign extension.
func GetI8(buf []byte, offBits int, lenBits uint8) int8 {
	sat := uint8(ChooseMin(int(lenBits), 8))
	val := uint64(GetU8(buf, offBits, sat))
	return int8(signExtendU64(val, sat))
}

// GetI16 deserializes up to 16 bits as a signed integer with sign extension.
func GetI16(buf []byte, offBits int, lenBits uint8) int16 {
	sat := uint8(ChooseMin(int(lenBits), 16))
	val := uint64(GetU16(buf, offBits, sat))
	return int16(signExtendU64(val, sat))
}

// GetI32 deserializes up to 32 bits as a signed integer with sign extension.
func GetI32(buf []byte, offBits int, lenBits uint8) int32 {
	sat := uint8(ChooseMin(int(lenBits), 32))
	val := uint64(GetU32(buf, offBits, sat))
	return int32(signExtendU64(val, sat))
}

// GetI64 deserializes up to 64 bits as a signed integer with sign extension.
func GetI64(buf []byte, offBits int, lenBits uint8) int64 {
	sat := uint8(ChooseMin(int(lenBits), 64))
	val := GetU64(buf, offBits, sat)
	return int64(signExtendU64(val, sat))
}

// Float16Pack converts an IEEE-754 float32 into binary16 encoding.
func Float16Pack(value float32) uint16 {
	const roundMask uint32 = ^uint32(0x0FFF)
	const f32inf uint32 = 255 << 23
	const f16inf uint32 = 31 << 23
	const magic uint32 = 15 << 23

	inBits := math.Float32bits(value)
	sign := inBits & (1 << 31)
	inBits ^= sign
	var out uint16

	if inBits >= f32inf {
		if (inBits & 0x7F_FFFF) != 0 {
			out = 0x7E00
		} else if inBits > f32inf {
			out = 0x7FFF
		} else {
			out = 0x7C00
		}
	} else {
		inBits &= roundMask
		scaled := math.Float32frombits(inBits) * math.Float32frombits(magic)
		inBits = math.Float32bits(scaled)
		// Unsigned wraparound is load-bearing here (matches the C reference
		// `in.bits -= round_mask`): for finite inputs `inBits < roundMask`, so the
		// subtraction underflows and wraps, which produces the correct mantissa.
		// A guarded/saturating subtraction would zero every finite value instead.
		inBits -= roundMask
		if inBits > f16inf {
			inBits = f16inf
		}
		out = uint16(inBits >> 13)
	}

	return out | uint16(sign>>16)
}

// Float16Unpack converts a binary16 encoding into IEEE-754 float32.
func Float16Unpack(value uint16) float32 {
	const magic uint32 = 0xEF << 23
	const infNan uint32 = 0x8F << 23
	outBits := (uint32(value&0x7FFF) << 13)
	scaled := math.Float32frombits(outBits) * math.Float32frombits(magic)
	outBits = math.Float32bits(scaled)
	if scaled >= math.Float32frombits(infNan) {
		outBits |= 0xFF << 23
	}
	outBits |= uint32(value&0x8000) << 16
	return math.Float32frombits(outBits)
}

// SetF16 serializes value as binary16 at offBits.
// It returns DSDL_RUNTIME_SUCCESS on success or a negative runtime error code.
func SetF16(buf []byte, offBits int, value float32) int8 {
	return SetUxx(buf, offBits, uint64(Float16Pack(value)), 16)
}

// GetF16 deserializes a binary16 value at offBits.
func GetF16(buf []byte, offBits int) float32 {
	return Float16Unpack(GetU16(buf, offBits, 16))
}

// SetF32 serializes value as IEEE-754 binary32 at offBits.
// It returns DSDL_RUNTIME_SUCCESS on success or a negative runtime error code.
func SetF32(buf []byte, offBits int, value float32) int8 {
	return SetUxx(buf, offBits, uint64(math.Float32bits(value)), 32)
}

// GetF32 deserializes an IEEE-754 binary32 value at offBits.
func GetF32(buf []byte, offBits int) float32 {
	return math.Float32frombits(GetU32(buf, offBits, 32))
}

// SetF64 serializes value as IEEE-754 binary64 at offBits.
// It returns DSDL_RUNTIME_SUCCESS on success or a negative runtime error code.
func SetF64(buf []byte, offBits int, value float64) int8 {
	return SetUxx(buf, offBits, math.Float64bits(value), 64)
}

// GetF64 deserializes an IEEE-754 binary64 value at offBits.
func GetF64(buf []byte, offBits int) float64 {
	return math.Float64frombits(GetU64(buf, offBits, 64))
}
