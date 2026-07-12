#![allow(clippy::cast_possible_truncation)]
#![allow(clippy::cast_sign_loss)]

//! Portable bit-level runtime helpers for generated DSDL Rust bindings.
//!
//! This module provides integer/float serialization primitives and bounded
//! bit-copy helpers shared by generated serializers and deserializers.

use core::fmt;
use core::ops::{Deref, DerefMut, Index, IndexMut};

// Semantic wrappers above primitive runtime operations are generated from
// `runtime/rust/dsdl_runtime_semantic_wrappers.rs.in`.
include!("dsdl_runtime_semantic_wrappers.rs");

#[inline]
/// Returns the smaller of two values.
pub fn choose_min(a: usize, b: usize) -> usize {
    if a < b {
        a
    } else {
        b
    }
}

#[inline]
/// Saturates a requested bit fragment to fit inside a bounded byte buffer.
pub fn saturate_fragment_bits(
    buffer_size_bytes: usize,
    fragment_offset_bits: usize,
    fragment_length_bits: usize,
) -> usize {
    let size_bits = buffer_size_bytes.saturating_mul(8);
    let tail_bits = size_bits.saturating_sub(choose_min(size_bits, fragment_offset_bits));
    choose_min(fragment_length_bits, tail_bits)
}

#[inline]
/// Portable bit-copy implementation used when fast-path conditions do not hold.
fn copy_bits_portable(
    dst: &mut [u8],
    dst_offset_bits: usize,
    length_bits: usize,
    src: &[u8],
    src_offset_bits: usize,
) {
    for i in 0..length_bits {
        let src_bit_index = src_offset_bits + i;
        if (src_bit_index / 8) >= src.len() {
            break;
        }

        let dst_bit_index = dst_offset_bits + i;
        if (dst_bit_index / 8) >= dst.len() {
            break;
        }

        let bit = ((src[src_bit_index / 8] >> (src_bit_index % 8)) & 1u8) != 0;
        if bit {
            dst[dst_bit_index / 8] |= 1u8 << (dst_bit_index % 8);
        } else {
            dst[dst_bit_index / 8] &= !(1u8 << (dst_bit_index % 8));
        }
    }
}

#[inline]
#[cfg(feature = "runtime-fast")]
/// Optimized bit-copy implementation with aligned-copy fast path.
fn copy_bits_runtime_fast(
    dst: &mut [u8],
    dst_offset_bits: usize,
    length_bits: usize,
    src: &[u8],
    src_offset_bits: usize,
) {
    if length_bits == 0 {
        return;
    }
    if (dst_offset_bits | src_offset_bits | length_bits) & 7 == 0 {
        let dst_start = dst_offset_bits / 8;
        let src_start = src_offset_bits / 8;
        if dst_start <= dst.len() && src_start <= src.len() {
            let dst_available = dst.len().saturating_sub(dst_start);
            let src_available = src.len().saturating_sub(src_start);
            let requested_bytes = length_bits / 8;
            let copy_bytes = choose_min(requested_bytes, choose_min(dst_available, src_available));
            if copy_bytes > 0 {
                dst[dst_start..dst_start + copy_bytes]
                    .copy_from_slice(&src[src_start..src_start + copy_bytes]);
            }
            return;
        }
    }
    copy_bits_portable(dst, dst_offset_bits, length_bits, src, src_offset_bits);
}

#[inline]
/// Copies `length_bits` from `src` to `dst` using DSDL bit ordering.
///
/// When `runtime-fast` is enabled, aligned copies are accelerated.
pub fn copy_bits(
    dst: &mut [u8],
    dst_offset_bits: usize,
    length_bits: usize,
    src: &[u8],
    src_offset_bits: usize,
) {
    #[cfg(feature = "runtime-fast")]
    {
        copy_bits_runtime_fast(dst, dst_offset_bits, length_bits, src, src_offset_bits);
        return;
    }

    #[cfg(not(feature = "runtime-fast"))]
    {
        copy_bits_portable(dst, dst_offset_bits, length_bits, src, src_offset_bits);
    }
}

#[inline]
/// Reads a bit fragment from `buf` into `output` with implicit zero extension.
pub fn get_bits(output: &mut [u8], buf: &[u8], off_bits: usize, len_bits: usize) {
    let sat_bits = saturate_fragment_bits(buf.len(), off_bits, len_bits);
    let out_len = (len_bits + 7) / 8;
    for b in output.iter_mut().take(out_len) {
        *b = 0;
    }
    copy_bits(output, 0, sat_bits, buf, off_bits);
}

#[inline]
/// Serializes one boolean bit into `buf` at `off_bits`.
///
/// Returns [`DSDL_RUNTIME_SUCCESS`] on success or a negative error code.
pub fn set_bit(buf: &mut [u8], off_bits: usize, value: bool) -> i8 {
    if buf.len().saturating_mul(8) <= off_bits {
        return -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
    }
    let val = if value { 1u8 } else { 0u8 };
    copy_bits(buf, off_bits, 1, &[val], 0);
    DSDL_RUNTIME_SUCCESS
}

#[inline]
/// Serializes an unsigned integer fragment of `len_bits` width.
///
/// Returns [`DSDL_RUNTIME_SUCCESS`] on success or a negative error code.
pub fn set_uxx(buf: &mut [u8], off_bits: usize, value: u64, len_bits: u8) -> i8 {
    if buf.len().saturating_mul(8) < off_bits.saturating_add(len_bits as usize) {
        return -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
    }
    let saturated_len_bits = choose_min(len_bits as usize, 64);
    let tmp = value.to_le_bytes();
    copy_bits(buf, off_bits, saturated_len_bits, &tmp, 0);
    DSDL_RUNTIME_SUCCESS
}

#[inline]
/// Serializes a signed integer fragment of `len_bits` width.
pub fn set_ixx(buf: &mut [u8], off_bits: usize, value: i64, len_bits: u8) -> i8 {
    set_uxx(buf, off_bits, value as u64, len_bits)
}

#[inline]
/// Deserializes one bit as boolean.
pub fn get_bit(buf: &[u8], off_bits: usize) -> bool {
    get_u8(buf, off_bits, 1) == 1
}

#[inline]
/// Deserializes up to 8 bits as an unsigned integer.
pub fn get_u8(buf: &[u8], off_bits: usize, len_bits: u8) -> u8 {
    let bits = saturate_fragment_bits(buf.len(), off_bits, choose_min(len_bits as usize, 8));
    let mut out = [0u8; 1];
    copy_bits(&mut out, 0, bits, buf, off_bits);
    out[0]
}

#[inline]
/// Deserializes up to 16 bits as an unsigned integer.
pub fn get_u16(buf: &[u8], off_bits: usize, len_bits: u8) -> u16 {
    let bits = saturate_fragment_bits(buf.len(), off_bits, choose_min(len_bits as usize, 16));
    let mut out = [0u8; 2];
    copy_bits(&mut out, 0, bits, buf, off_bits);
    u16::from_le_bytes(out)
}

#[inline]
/// Deserializes up to 32 bits as an unsigned integer.
pub fn get_u32(buf: &[u8], off_bits: usize, len_bits: u8) -> u32 {
    let bits = saturate_fragment_bits(buf.len(), off_bits, choose_min(len_bits as usize, 32));
    let mut out = [0u8; 4];
    copy_bits(&mut out, 0, bits, buf, off_bits);
    u32::from_le_bytes(out)
}

#[inline]
/// Deserializes up to 64 bits as an unsigned integer.
pub fn get_u64(buf: &[u8], off_bits: usize, len_bits: u8) -> u64 {
    let bits = saturate_fragment_bits(buf.len(), off_bits, choose_min(len_bits as usize, 64));
    let mut out = [0u8; 8];
    copy_bits(&mut out, 0, bits, buf, off_bits);
    u64::from_le_bytes(out)
}

#[inline]
/// Sign-extends an unsigned integer interpreted with `sat` significant bits.
fn sign_extend_u64(value: u64, sat: u8) -> u64 {
    if sat == 0 || sat >= 64 {
        return value;
    }
    let sign_bit = 1u64 << (sat - 1);
    if (value & sign_bit) == 0 {
        return value;
    }
    value | (!0u64 << sat)
}

#[inline]
/// Deserializes up to 8 bits as a signed integer.
pub fn get_i8(buf: &[u8], off_bits: usize, len_bits: u8) -> i8 {
    let sat = choose_min(len_bits as usize, 8) as u8;
    let val = get_u8(buf, off_bits, sat) as u64;
    sign_extend_u64(val, sat) as i8
}

#[inline]
/// Deserializes up to 16 bits as a signed integer.
pub fn get_i16(buf: &[u8], off_bits: usize, len_bits: u8) -> i16 {
    let sat = choose_min(len_bits as usize, 16) as u8;
    let val = get_u16(buf, off_bits, sat) as u64;
    sign_extend_u64(val, sat) as i16
}

#[inline]
/// Deserializes up to 32 bits as a signed integer.
pub fn get_i32(buf: &[u8], off_bits: usize, len_bits: u8) -> i32 {
    let sat = choose_min(len_bits as usize, 32) as u8;
    let val = get_u32(buf, off_bits, sat) as u64;
    sign_extend_u64(val, sat) as i32
}

#[inline]
/// Deserializes up to 64 bits as a signed integer.
pub fn get_i64(buf: &[u8], off_bits: usize, len_bits: u8) -> i64 {
    let sat = choose_min(len_bits as usize, 64) as u8;
    let val = get_u64(buf, off_bits, sat);
    sign_extend_u64(val, sat) as i64
}

/// Converts an IEEE-754 `f32` value into binary16 encoding.
pub fn float16_pack(value: f32) -> u16 {
    let round_mask: u32 = !0x0FFF;
    let f32inf: u32 = 255u32 << 23;
    let f16inf: u32 = 31u32 << 23;
    let magic: u32 = 15u32 << 23;

    let mut in_bits = value.to_bits();
    let sign = in_bits & (1u32 << 31);
    in_bits ^= sign;
    let out: u16;

    if in_bits >= f32inf {
        out = if (in_bits & 0x7F_FFFF) != 0 {
            0x7E00
        } else if in_bits > f32inf {
            0x7FFF
        } else {
            0x7C00
        };
    } else {
        in_bits &= round_mask;
        let scaled = f32::from_bits(in_bits) * f32::from_bits(magic);
        in_bits = scaled.to_bits();
        // Unsigned wraparound is load-bearing here (matches the C reference
        // `in.bits -= round_mask`): for finite inputs `in_bits < round_mask`, so the
        // subtraction underflows and wraps, which produces the correct mantissa. A
        // saturating subtraction would zero every finite value instead.
        in_bits = in_bits.wrapping_sub(round_mask);
        if in_bits > f16inf {
            in_bits = f16inf;
        }
        out = (in_bits >> 13) as u16;
    }

    out | ((sign >> 16) as u16)
}

/// Converts a binary16 value into IEEE-754 `f32`.
pub fn float16_unpack(value: u16) -> f32 {
    let magic = 0xEFu32 << 23;
    let inf_nan = 0x8Fu32 << 23;
    let mut out_bits = ((value & 0x7FFF) as u32) << 13;
    let scaled = f32::from_bits(out_bits) * f32::from_bits(magic);
    out_bits = scaled.to_bits();
    if scaled >= f32::from_bits(inf_nan) {
        out_bits |= 0xFFu32 << 23;
    }
    out_bits |= ((value & 0x8000) as u32) << 16;
    f32::from_bits(out_bits)
}

#[inline]
/// Serializes `value` as binary16 at `off_bits`.
pub fn set_f16(buf: &mut [u8], off_bits: usize, value: f32) -> i8 {
    set_uxx(buf, off_bits, float16_pack(value) as u64, 16)
}

#[inline]
/// Deserializes a binary16 value at `off_bits`.
pub fn get_f16(buf: &[u8], off_bits: usize) -> f32 {
    float16_unpack(get_u16(buf, off_bits, 16))
}

#[inline]
/// Serializes `value` as IEEE-754 binary32 at `off_bits`.
pub fn set_f32(buf: &mut [u8], off_bits: usize, value: f32) -> i8 {
    set_uxx(buf, off_bits, value.to_bits() as u64, 32)
}

#[inline]
/// Deserializes an IEEE-754 binary32 value at `off_bits`.
pub fn get_f32(buf: &[u8], off_bits: usize) -> f32 {
    f32::from_bits(get_u32(buf, off_bits, 32))
}

#[inline]
/// Serializes `value` as IEEE-754 binary64 at `off_bits`.
pub fn set_f64(buf: &mut [u8], off_bits: usize, value: f64) -> i8 {
    set_uxx(buf, off_bits, value.to_bits(), 64)
}

#[inline]
/// Deserializes an IEEE-754 binary64 value at `off_bits`.
pub fn get_f64(buf: &[u8], off_bits: usize) -> f64 {
    f64::from_bits(get_u64(buf, off_bits, 64))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn var_array_vec_compatible_surface() {
        let mut values = DsdlVec::<u8>::with_capacity(2);
        assert_eq!(values.len(), 0);
        assert!(values.is_empty());
        values.push(3);
        values.push(5);
        values.resize(4, 9);
        assert_eq!(values.len(), 4);
        assert_eq!(values[0], 3);
        assert_eq!(values[1], 5);
        assert_eq!(values[2], 9);
        assert_eq!(values[3], 9);
        values.clear();
        assert!(values.is_empty());
    }

    #[test]
    fn reserve_with_pool_max_inline_does_not_allocate() {
        let mut values = DsdlVec::<u8>::with_contract(VarArrayMemoryContract::new(
            DsdlMemoryMode::MaxInline,
            8,
            AllocationClassId(7),
        ));
        let mut pool = BudgetPoolProvider::new(16);
        values
            .reserve_with_pool(64, &mut pool)
            .expect("max-inline reserve_with_pool should not hit pool allocation");
        assert_eq!(pool.allocations(), 0);
        assert_eq!(pool.remaining_bytes(), 16);
    }

    #[test]
    fn reserve_with_pool_inline_then_pool_threshold_behavior() {
        let contract =
            VarArrayMemoryContract::new(DsdlMemoryMode::InlineThenPool, 16, AllocationClassId(42));
        let mut values = DsdlVec::<u8>::with_contract(contract);
        let mut pool = BudgetPoolProvider::new(32);

        values
            .reserve_with_pool(8, &mut pool)
            .expect("below-threshold reserve should not allocate");
        assert_eq!(pool.allocations(), 0);
        assert_eq!(pool.remaining_bytes(), 32);

        values
            .reserve_with_pool(24, &mut pool)
            .expect("above-threshold reserve should allocate");
        assert_eq!(pool.allocations(), 1);
        assert_eq!(pool.remaining_bytes(), 24);
        assert_eq!(pool.total_allocated_bytes(), 8);
    }

    #[test]
    fn reserve_with_pool_inline_then_pool_oom_reports_taxonomy() {
        let contract =
            VarArrayMemoryContract::new(DsdlMemoryMode::InlineThenPool, 8, AllocationClassId(11));
        let mut values = DsdlVec::<u16>::with_contract(contract);
        let mut pool = BudgetPoolProvider::new(4);

        let err = values
            .reserve_with_pool(8, &mut pool)
            .expect_err("reserve_with_pool should fail with out-of-memory");
        assert_eq!(err.kind, AllocationErrorKind::OutOfMemory);
        assert_eq!(err.class_id, AllocationClassId(11));
        assert_eq!(
            allocation_error_to_runtime_code(err),
            DSDL_RUNTIME_ERROR_ALLOCATION_OUT_OF_MEMORY
        );
    }

    #[test]
    fn null_pool_provider_reports_unavailable() {
        let mut null_pool = NullPoolProvider;
        let err = PoolProvider::alloc(&mut null_pool, AllocationClassId(3), 16, 8)
            .expect_err("null provider should always fail allocations");
        assert_eq!(err.kind, AllocationErrorKind::PoolUnavailable);
        assert_eq!(err.class_id, AllocationClassId(3));
        assert_eq!(err.requested_bytes, 16);
        assert_eq!(
            allocation_error_to_runtime_code(err),
            DSDL_RUNTIME_ERROR_ALLOCATION_POOL_UNAVAILABLE
        );
    }

    #[test]
    fn budget_pool_provider_rejects_invalid_alignment() {
        let mut pool = BudgetPoolProvider::new(32);
        let err = PoolProvider::alloc(&mut pool, AllocationClassId(9), 12, 3)
            .expect_err("non-power-of-two alignment should be rejected");
        assert_eq!(err.kind, AllocationErrorKind::InvalidRequest);
        assert_eq!(err.class_id, AllocationClassId(9));
        assert_eq!(err.requested_bytes, 12);
        assert_eq!(
            allocation_error_to_runtime_code(err),
            DSDL_RUNTIME_ERROR_ALLOCATION_INVALID_REQUEST
        );
    }

    #[test]
    fn reserve_with_pool_failure_keeps_existing_state() {
        let contract =
            VarArrayMemoryContract::new(DsdlMemoryMode::InlineThenPool, 0, AllocationClassId(77));
        let mut values = DsdlVec::<u8>::with_contract(contract);
        values.push(11);
        values.push(22);
        values.push(33);
        let capacity_before = values.capacity();

        let mut pool = BudgetPoolProvider::new(1);
        let err = values
            .reserve_with_pool(8, &mut pool)
            .expect_err("tiny budget should force deterministic OOM");
        assert_eq!(err.kind, AllocationErrorKind::OutOfMemory);
        assert_eq!(values.as_slice(), &[11, 22, 33]);
        assert_eq!(values.len(), 3);
        assert_eq!(values.capacity(), capacity_before);
        assert_eq!(pool.allocations(), 0);
    }

    #[test]
    fn reserve_with_pool_zst_never_allocates() {
        let contract =
            VarArrayMemoryContract::new(DsdlMemoryMode::InlineThenPool, 0, AllocationClassId(3));
        let mut values = DsdlVec::<()>::with_contract(contract);
        let mut pool = BudgetPoolProvider::new(0);
        values
            .reserve_with_pool(1024, &mut pool)
            .expect("zero-sized elements should not allocate from pool");
        assert_eq!(values.len(), 0);
        assert_eq!(pool.allocations(), 0);
        assert_eq!(pool.remaining_bytes(), 0);
    }

    #[test]
    fn tiny_budget_failures_are_deterministic_across_calls() {
        let contract =
            VarArrayMemoryContract::new(DsdlMemoryMode::InlineThenPool, 0, AllocationClassId(5));
        let mut values = DsdlVec::<u8>::with_contract(contract);
        let mut pool = BudgetPoolProvider::new(4);

        values
            .reserve_with_pool(4, &mut pool)
            .expect("first reserve should fit tiny budget");
        assert_eq!(pool.allocations(), 1);
        assert_eq!(pool.remaining_bytes(), 0);

        let err = values
            .reserve_with_pool(8, &mut pool)
            .expect_err("second reserve should deterministically fail after budget exhaustion");
        assert_eq!(err.kind, AllocationErrorKind::OutOfMemory);
        assert_eq!(err.class_id, AllocationClassId(5));
        assert_eq!(err.requested_bytes, 8);
        assert_eq!(pool.allocations(), 1);
        assert_eq!(pool.remaining_bytes(), 0);
    }

    #[test]
    fn allocation_error_diagnostics_are_actionable() {
        let err = AllocationError::new(AllocationErrorKind::OutOfMemory, AllocationClassId(19), 48);
        let rendered = err.to_string();
        assert!(rendered.contains("allocation_error(kind=out_of_memory"));
        assert!(rendered.contains("class_id=19"));
        assert!(rendered.contains("requested_bytes=48"));
        assert_eq!(
            allocation_error_hint(AllocationErrorKind::OutOfMemory),
            "Increase per-type pool budget or use max-inline mode for deterministic pre-allocation."
        );
        assert_eq!(
            allocation_error_hint(AllocationErrorKind::PoolUnavailable),
            "Enable/configure a pool provider for inline-then-pool mode, or switch to max-inline mode."
        );
    }
}
