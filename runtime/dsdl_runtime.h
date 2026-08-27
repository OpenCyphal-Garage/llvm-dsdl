//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// C/C++ DSDL serialization runtime primitives shared by generated code.
///
/// This header provides bit-copy, integer, and floating-point helpers used by
/// generated serializers and deserializers. The implementation is intentionally
/// header-only and aligned with OpenCyphal/Nunavut runtime semantics so that
/// generated code remains portable across integration environments.
///
//===----------------------------------------------------------------------===//

#ifndef LLVMDSDL_RUNTIME_DSDL_RUNTIME_H
#define LLVMDSDL_RUNTIME_DSDL_RUNTIME_H

#ifdef __cplusplus
#    if (__cplusplus < 201402L)
#        error "Unsupported language: ISO C11, C++14, or a newer version of either is required."
#    endif

extern "C"
{
#else
#    if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
#        error "Unsupported language: ISO C11 or a newer version is required."
#    endif
#endif

#include <string.h>

#include <float.h>
#include <math.h>  // For isfinite().
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>  // For _Static_assert (C11) static_assert (C23) and assert() if DSDL_RUNTIME_ASSERT is used.

// `_Static_assert` is a C11 keyword, so defining a macro of that name for the C++ build declares a
// reserved identifier -- which `-Wreserved-identifier` rejects in any translation unit that includes
// this header. Both languages spell the assertion natively; this picks the spelling instead.
#ifdef __cplusplus
#    define DSDL_RUNTIME_STATIC_ASSERT(TERM, MESSAGE) static_assert(TERM, MESSAGE)
#else
#    define DSDL_RUNTIME_STATIC_ASSERT(TERM, MESSAGE) _Static_assert(TERM, MESSAGE)
#endif

    DSDL_RUNTIME_STATIC_ASSERT(sizeof(size_t) >= sizeof(size_t), "Unexpected target size_t width");

/// @brief Runtime success code.
///
/// Runtime helpers return `0` for success and a negative error code for
/// failures.
#define DSDL_RUNTIME_SUCCESS 0

/// @brief API usage error code for invalid arguments.
#define DSDL_RUNTIME_ERROR_INVALID_ARGUMENT 2

/// @brief API usage error code for insufficient serialization buffer size.
#define DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL 3

/// @brief Representation error code for invalid array-length values.
#define DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH 10

/// @brief Representation error code for invalid union tag values.
#define DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG 11

/// @brief Representation error code for malformed delimiter headers.
#define DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER 12

/// @brief Compile-time check for IEEE-754 single-precision compatibility.
#define DSDL_RUNTIME_PLATFORM_IEEE754_FLOAT \
    ((FLT_RADIX == 2) && (FLT_MANT_DIG == 24) && (FLT_MIN_EXP == -125) && (FLT_MAX_EXP == 128))

/// @brief Compile-time check for IEEE-754 double-precision compatibility.
#define DSDL_RUNTIME_PLATFORM_IEEE754_DOUBLE \
    ((FLT_RADIX == 2) && (DBL_MANT_DIG == 53) && (DBL_MIN_EXP == -1021) && (DBL_MAX_EXP == 1024))

#ifndef DSDL_RUNTIME_ASSERT
#    define DSDL_RUNTIME_ASSERT(x) assert(x)
#endif

    // This code is endianness-invariant. Use target_endianness='little' to generate little-endian-optimized code.

    // ---------------------------------------------------- HELPERS ----------------------------------------------------

    /// @brief Returns the smaller of two values.
    /// @param[in] a First value.
    /// @param[in] b Second value.
    /// @return The minimum of `a` and `b`.
    static inline size_t dsdl_runtime_choose_min(const size_t a, const size_t b)
    {
        return (a < b) ? a : b;
    }

    /// @brief Computes a safe bit-copy length for a bounded byte buffer.
    ///
    /// @details Buffer size is expressed in bytes, while offsets and lengths are
    /// expressed in bits. This helper saturates the requested bit length to what
    /// can be consumed from `fragment_offset_bits` to the end of the buffer.
    ///
    /// @param[in] buffer_size_bytes Total source/destination buffer size in bytes.
    /// @param[in] fragment_offset_bits Requested fragment start offset in bits.
    /// @param[in] fragment_length_bits Requested fragment length in bits.
    /// @return Saturated bit count that can be safely read/written.
    ///
    ///
    ///      buffer                                                                buffer
    ///      origin                                                                 end
    ///         [------------------------- buffer_size_bytes ------------------------]
    ///         [--------------- fragment_offset_bits ---------------][--- fragment_length_bits ---]
    ///                                                               [-- out bits --]
    ///
    static inline size_t dsdl_runtime_saturate_fragment_bits(const size_t buffer_size_bytes,
                                                             const size_t fragment_offset_bits,
                                                             const size_t fragment_length_bits)
    {
        const size_t size_bits = (size_t) buffer_size_bytes * 8U;
        const size_t tail_bits = size_bits - dsdl_runtime_choose_min(size_bits, fragment_offset_bits);
        return dsdl_runtime_choose_min(fragment_length_bits, tail_bits);
    }

    // ---------------------------------------------------- BIT ARRAY
    // ----------------------------------------------------

    /// @brief Copies a bit fragment from `src` into `dst` using DSDL bit order.
    ///
    /// @details
    /// Offsets are bit-based and may be unaligned. Byte-aligned copies use an
    /// optimized `memmove` path; unaligned copies use a bitwise transfer path.
    ///
    /// If `src` and `dst` overlap while offsets are not byte-aligned, behaviour is
    /// undefined.
    ///
    /// @param[out] dst Destination buffer.
    /// @param[in] dst_offset_bits Destination offset in bits.
    /// @param[in] length_bits Number of bits to copy.
    /// @param[in] src Source buffer.
    /// @param[in] src_offset_bits Source offset in bits.
    static inline void dsdl_runtime_copy_bits(void* const       dst,
                                              const size_t      dst_offset_bits,
                                              const size_t      length_bits,
                                              const void* const src,
                                              const size_t      src_offset_bits)
    {
        DSDL_RUNTIME_ASSERT(src != NULL);
        DSDL_RUNTIME_ASSERT(dst != NULL);
        DSDL_RUNTIME_ASSERT(src != dst);
        if ((0U == (src_offset_bits % 8U)) &&
            (0U == (dst_offset_bits % 8U)))  // Aligned copy, optimized, most common case.
        {
            const size_t length_bytes = (size_t) (length_bits / 8U);

            // Intentional violation of MISRA: Pointer arithmetics. This is done to remove the API constraint that
            // offsets be under 8 bits. Fewer constraints reduce the chance of API misuse.
            const uint8_t* const psrc = (src_offset_bits / 8U) + (const uint8_t*) src;  // NOSONAR NOLINT
            uint8_t* const       pdst = (dst_offset_bits / 8U) + (uint8_t*) dst;        // NOSONAR NOLINT
            if (length_bytes > 0U)                                                      // issue #337 workaround
            {
                (void) memmove(pdst, psrc, length_bytes);
            }
            const uint8_t length_mod = (uint8_t) (length_bits % 8U);
            if (0U != length_mod)  // If the length is unaligned, the last byte requires special treatment.
            {
                // Intentional violation of MISRA: Pointer arithmetics. It is unavoidable in this context.
                const uint8_t* const last_src = psrc + length_bytes;  // NOLINT NOSONAR
                uint8_t* const       last_dst = pdst + length_bytes;  // NOLINT NOSONAR
                DSDL_RUNTIME_ASSERT(length_mod < 8U);
                const uint8_t mask = (uint8_t) ((1U << length_mod) - 1U);

                // No lint for "The left operand of '&' is a garbage value" because
                // these so called "garbage" bits of `*last_dst` won't be used during deserialization.
                // NOLINTNEXTLINE(clang-analyzer-core.UndefinedBinaryOperatorResult)
                *last_dst = (*last_dst & (uint8_t) ~mask) | (*last_src & mask);
            }
        }
        else
        {
            // The algorithm was originally designed by Ben Dyer for Libuavcan v0:
            // https://github.com/OpenCyphal/libuavcan/blob/legacy-v0/libuavcan/src/marshal/uc_bit_array_copy.cpp
            // This version is modified for v1 where the bit order is the opposite.
            const uint8_t* const psrc     = (const uint8_t*) src;
            uint8_t* const       pdst     = (uint8_t*) dst;
            size_t               src_off  = src_offset_bits;
            size_t               dst_off  = dst_offset_bits;
            const size_t         last_bit = src_off + length_bits;
            DSDL_RUNTIME_ASSERT(
                ((psrc < pdst) ? ((uintptr_t) (psrc + ((src_offset_bits + length_bits + 7U) / 8U)) <= (uintptr_t) pdst)
                               : 1));
            DSDL_RUNTIME_ASSERT(
                ((psrc > pdst) ? ((uintptr_t) (pdst + ((dst_offset_bits + length_bits + 7U) / 8U)) <= (uintptr_t) psrc)
                               : 1));
            while (last_bit > src_off)
            {
                const uint8_t src_mod = (uint8_t) (src_off % 8U);
                const uint8_t dst_mod = (uint8_t) (dst_off % 8U);
                const uint8_t max_mod = (src_mod > dst_mod) ? src_mod : dst_mod;
                const uint8_t size    = (uint8_t) dsdl_runtime_choose_min(8U - max_mod, last_bit - src_off);
                DSDL_RUNTIME_ASSERT(size > 0U);
                DSDL_RUNTIME_ASSERT(size <= 8U);

                // Suppress a false warning from Clang-Tidy & Sonar that size is being over-shifted. It's not.
                const uint8_t mask = (uint8_t) ((((1U << size) - 1U) << dst_mod) & 0xFFU);  // NOLINT NOSONAR
                DSDL_RUNTIME_ASSERT(mask > 0U);

                // Intentional violation of MISRA: indexing on a pointer.
                // This simplifies the implementation greatly and avoids pointer arithmetics.
                const uint8_t in = (uint8_t) ((uint8_t) (psrc[src_off / 8U] >> src_mod) << dst_mod) & 0xFFU;  // NOSONAR

                // Intentional violation of MISRA: indexing on a pointer.
                // This simplifies the implementation greatly and avoids pointer arithmetics.
                const uint8_t a = pdst[dst_off / 8U] & ((uint8_t) ~mask);  // NOSONAR
                const uint8_t b = in & mask;

                // Intentional violation of MISRA: indexing on a pointer.
                // This simplifies the implementation greatly and avoids pointer arithmetics.
                pdst[dst_off / 8U] = a | b;  // NOSONAR
                src_off += size;
                dst_off += size;
            }
            DSDL_RUNTIME_ASSERT(last_bit == src_off);
        }
    }

    /// @brief Reads a bit fragment and zero-extends out-of-range data.
    ///
    /// @details
    /// This helper is used by primitive deserializers. If
    /// `(off_bits + len_bits)` exceeds `buf_size_bytes * 8`, missing bits are
    /// treated as zero (implicit zero extension).
    ///
    /// @param[out] output Destination byte array for extracted bits.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source offset in bits.
    /// @param[in] len_bits Requested bit count.
    static inline void dsdl_runtime_get_bits(void* const       output,
                                             const void* const buf,
                                             const size_t      buf_size_bytes,
                                             const size_t      off_bits,
                                             const size_t      len_bits)
    {
        DSDL_RUNTIME_ASSERT(output != NULL);
        DSDL_RUNTIME_ASSERT(buf != NULL);
        const size_t sat_bits = dsdl_runtime_saturate_fragment_bits(buf_size_bytes, off_bits, len_bits);

        // Apply implicit zero extension. Normally, this is a no-op unless (len_bits > sat_bits) or (len_bits % 8 != 0).
        // The former case ensures that if we're copying <8 bits, the MSB in the destination will be zeroed out.
        (void) memset(((uint8_t*) output) + (sat_bits / 8U), 0, ((len_bits + 7U) / 8U) - (sat_bits / 8U));
        dsdl_runtime_copy_bits(output, 0U, sat_bits, buf, off_bits);
    }

    // ---------------------------------------------------- INTEGER ----------------------------------------------------

    /// @brief Serializes a one-bit boolean value at `off_bits`.
    /// @param[out] buf Destination serialized buffer.
    /// @param[in] buf_size_bytes Destination buffer size in bytes.
    /// @param[in] off_bits Destination bit offset.
    /// @param[in] value Boolean value to serialize.
    /// @return `DSDL_RUNTIME_SUCCESS` or a negative error code.
    static inline int8_t dsdl_runtime_set_bit(uint8_t* const buf,
                                              const size_t   buf_size_bytes,
                                              const size_t   off_bits,
                                              const bool     value)
    {
        DSDL_RUNTIME_ASSERT(buf != NULL);
        if ((buf_size_bytes * 8) <= off_bits)
        {
            return -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
        }
        const uint8_t val = value ? 1U : 0U;
        dsdl_runtime_copy_bits(buf, off_bits, 1U, &val, 0U);
        return DSDL_RUNTIME_SUCCESS;
    }

    /// @brief Serializes an unsigned integer fragment in little-endian bit order.
    /// @param[out] buf Destination serialized buffer.
    /// @param[in] buf_size_bytes Destination buffer size in bytes.
    /// @param[in] off_bits Destination bit offset.
    /// @param[in] value Unsigned value to serialize.
    /// @param[in] len_bits Serialized bit width; values greater than 64 are
    /// saturated to 64.
    /// @return `DSDL_RUNTIME_SUCCESS` or a negative error code.
    static inline int8_t dsdl_runtime_set_uxx(uint8_t* const buf,
                                              const size_t   buf_size_bytes,
                                              const size_t   off_bits,
                                              const uint64_t value,
                                              const uint8_t  len_bits)
    {
        DSDL_RUNTIME_STATIC_ASSERT(64U == (sizeof(uint64_t) * 8U), "Unexpected size of uint64_t");
        DSDL_RUNTIME_ASSERT(buf != NULL);
        if ((buf_size_bytes * 8) < (off_bits + len_bits))
        {
            return -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
        }
        const size_t  saturated_len_bits    = dsdl_runtime_choose_min(len_bits, 64U);
        const uint8_t tmp[sizeof(uint64_t)] = {
            (uint8_t) ((value >> 0U) & 0xFFU),
            (uint8_t) ((value >> 8U) & 0xFFU),
            (uint8_t) ((value >> 16U) & 0xFFU),
            (uint8_t) ((value >> 24U) & 0xFFU),
            (uint8_t) ((value >> 32U) & 0xFFU),
            (uint8_t) ((value >> 40U) & 0xFFU),
            (uint8_t) ((value >> 48U) & 0xFFU),
            (uint8_t) ((value >> 56U) & 0xFFU),
        };
        dsdl_runtime_copy_bits(buf, off_bits, saturated_len_bits, &tmp[0], 0U);
        return DSDL_RUNTIME_SUCCESS;
    }

    /// @brief Serializes a signed integer fragment in little-endian bit order.
    /// @param[out] buf Destination serialized buffer.
    /// @param[in] buf_size_bytes Destination buffer size in bytes.
    /// @param[in] off_bits Destination bit offset.
    /// @param[in] value Signed value to serialize.
    /// @param[in] len_bits Serialized bit width.
    /// @return `DSDL_RUNTIME_SUCCESS` or a negative error code.
    static inline int8_t dsdl_runtime_set_ixx(uint8_t* const buf,
                                              const size_t   buf_size_bytes,
                                              const size_t   off_bits,
                                              const int64_t  value,
                                              const uint8_t  len_bits)
    {
        // The naive sign conversion is safe and portable according to the C standard:
        // 6.3.1.3.3: if the new type is unsigned, the value is converted by repeatedly adding or subtracting one more
        // than the maximum value that can be represented in the new type until the value is in the range of the new
        // type.
        return dsdl_runtime_set_uxx(buf, buf_size_bytes, off_bits, (uint64_t) value, len_bits);
    }

    /// @brief Deserializes an unsigned integer up to 8 bits wide.
    ///
    /// @details Reads beyond the end of the source buffer are implicitly
    /// zero-extended.
    ///
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @param[in] len_bits Requested bit width.
    /// @return Deserialized value.
    static inline uint8_t dsdl_runtime_get_u8(const uint8_t* const buf,
                                              const size_t         buf_size_bytes,
                                              const size_t         off_bits,
                                              const uint8_t        len_bits);

    /// @brief Deserializes a single bit as boolean.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @return `true` if the extracted bit is one; otherwise `false`.
    static inline bool dsdl_runtime_get_bit(const uint8_t* const buf,
                                            const size_t         buf_size_bytes,
                                            const size_t         off_bits)
    {
        return 1U == dsdl_runtime_get_u8(buf, buf_size_bytes, off_bits, 1U);
    }

    static inline uint8_t dsdl_runtime_get_u8(const uint8_t* const buf,
                                              const size_t         buf_size_bytes,
                                              const size_t         off_bits,
                                              const uint8_t        len_bits)
    {
        DSDL_RUNTIME_ASSERT(buf != NULL);
        const size_t bits =
            dsdl_runtime_saturate_fragment_bits(buf_size_bytes, off_bits, dsdl_runtime_choose_min(len_bits, 8U));
        DSDL_RUNTIME_ASSERT(bits <= (sizeof(uint8_t) * 8U));
        uint8_t val = 0;
        dsdl_runtime_copy_bits(&val, 0U, bits, buf, off_bits);
        return val;
    }

    /// @brief Deserializes an unsigned integer up to 16 bits wide.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @param[in] len_bits Requested bit width.
    /// @return Deserialized value.
    static inline uint16_t dsdl_runtime_get_u16(const uint8_t* const buf,
                                                const size_t         buf_size_bytes,
                                                const size_t         off_bits,
                                                const uint8_t        len_bits)
    {
        DSDL_RUNTIME_ASSERT(buf != NULL);
        const size_t bits =
            dsdl_runtime_saturate_fragment_bits(buf_size_bytes, off_bits, dsdl_runtime_choose_min(len_bits, 16U));
        DSDL_RUNTIME_ASSERT(bits <= (sizeof(uint16_t) * 8U));
        uint8_t tmp[sizeof(uint16_t)] = {0};
        dsdl_runtime_copy_bits(&tmp[0], 0U, bits, buf, off_bits);
        return (uint16_t) (tmp[0] | (uint16_t) (((uint16_t) tmp[1]) << 8U));
    }

    /// @brief Deserializes an unsigned integer up to 32 bits wide.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @param[in] len_bits Requested bit width.
    /// @return Deserialized value.
    static inline uint32_t dsdl_runtime_get_u32(const uint8_t* const buf,
                                                const size_t         buf_size_bytes,
                                                const size_t         off_bits,
                                                const uint8_t        len_bits)
    {
        DSDL_RUNTIME_ASSERT(buf != NULL);
        const size_t bits =
            dsdl_runtime_saturate_fragment_bits(buf_size_bytes, off_bits, dsdl_runtime_choose_min(len_bits, 32U));
        DSDL_RUNTIME_ASSERT(bits <= (sizeof(uint32_t) * 8U));
        uint8_t tmp[sizeof(uint32_t)] = {0};
        dsdl_runtime_copy_bits(&tmp[0], 0U, bits, buf, off_bits);
        return (uint32_t) (tmp[0] | ((uint32_t) tmp[1] << 8U) | ((uint32_t) tmp[2] << 16U) |
                           ((uint32_t) tmp[3] << 24U));
    }

    /// @brief Deserializes an unsigned integer up to 64 bits wide.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @param[in] len_bits Requested bit width.
    /// @return Deserialized value.
    static inline uint64_t dsdl_runtime_get_u64(const uint8_t* const buf,
                                                const size_t         buf_size_bytes,
                                                const size_t         off_bits,
                                                const uint8_t        len_bits)
    {
        DSDL_RUNTIME_ASSERT(buf != NULL);
        const size_t bits =
            dsdl_runtime_saturate_fragment_bits(buf_size_bytes, off_bits, dsdl_runtime_choose_min(len_bits, 64U));
        DSDL_RUNTIME_ASSERT(bits <= (sizeof(uint64_t) * 8U));
        uint8_t tmp[sizeof(uint64_t)] = {0};
        dsdl_runtime_copy_bits(&tmp[0], 0U, bits, buf, off_bits);
        return (uint64_t) (tmp[0] | ((uint64_t) tmp[1] << 8U) | ((uint64_t) tmp[2] << 16U) |
                           ((uint64_t) tmp[3] << 24U) | ((uint64_t) tmp[4] << 32U) | ((uint64_t) tmp[5] << 40U) |
                           ((uint64_t) tmp[6] << 48U) | ((uint64_t) tmp[7] << 56U));
    }

    /// @brief Deserializes a signed integer up to 8 bits wide.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @param[in] len_bits Requested bit width.
    /// @return Sign-extended deserialized value.
    static inline int8_t dsdl_runtime_get_i8(const uint8_t* const buf,
                                             const size_t         buf_size_bytes,
                                             const size_t         off_bits,
                                             const uint8_t        len_bits)
    {
        const uint8_t sat = (uint8_t) dsdl_runtime_choose_min(len_bits, 8U);
        uint8_t       val = dsdl_runtime_get_u8(buf, buf_size_bytes, off_bits, sat);
        const bool    neg = (sat > 0U) && ((val & (1ULL << (sat - 1U))) != 0U);
        if ((sat < 8U) && neg)
        {
            val = (uint8_t) (val | ~((1U << sat) - 1U));  // Sign extension
        }

        return neg ? (int8_t) ((-(int8_t) (uint8_t) ~val) - 1) : (int8_t) val;
    }

    /// @brief Deserializes a signed integer up to 16 bits wide.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @param[in] len_bits Requested bit width.
    /// @return Sign-extended deserialized value.
    static inline int16_t dsdl_runtime_get_i16(const uint8_t* const buf,
                                               const size_t         buf_size_bytes,
                                               const size_t         off_bits,
                                               const uint8_t        len_bits)
    {
        const uint8_t sat = (uint8_t) dsdl_runtime_choose_min(len_bits, 16U);
        uint16_t      val = dsdl_runtime_get_u16(buf, buf_size_bytes, off_bits, sat);
        const bool    neg = (sat > 0U) && ((val & (1ULL << (sat - 1U))) != 0U);
        if ((sat < 16U) && neg)
        {
            val = (uint16_t) (val | ~((1U << sat) - 1U));  // Sign extension
        }
        return neg ? (int16_t) ((-(int16_t) (uint16_t) ~val) - 1) : (int16_t) val;
    }

    /// @brief Deserializes a signed integer up to 32 bits wide.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @param[in] len_bits Requested bit width.
    /// @return Sign-extended deserialized value.
    static inline int32_t dsdl_runtime_get_i32(const uint8_t* const buf,
                                               const size_t         buf_size_bytes,
                                               const size_t         off_bits,
                                               const uint8_t        len_bits)
    {
        const uint8_t sat = (uint8_t) dsdl_runtime_choose_min(len_bits, 32U);
        uint32_t      val = dsdl_runtime_get_u32(buf, buf_size_bytes, off_bits, sat);
        const bool    neg = (sat > 0U) && ((val & (1ULL << (sat - 1U))) != 0U);
        if ((sat < 32U) && neg)
        {
            val = (uint32_t) (val | ~((1UL << sat) - 1U));  // Sign extension
        }
        return neg ? (int32_t) ((-(int32_t) ~val) - 1) : (int32_t) val;
    }

    /// @brief Deserializes a signed integer up to 64 bits wide.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @param[in] len_bits Requested bit width.
    /// @return Sign-extended deserialized value.
    static inline int64_t dsdl_runtime_get_i64(const uint8_t* const buf,
                                               const size_t         buf_size_bytes,
                                               const size_t         off_bits,
                                               const uint8_t        len_bits)
    {
        const uint8_t sat = (uint8_t) dsdl_runtime_choose_min(len_bits, 64U);
        uint64_t      val = dsdl_runtime_get_u64(buf, buf_size_bytes, off_bits, sat);
        const bool    neg = (sat > 0U) && ((val & (1ULL << (sat - 1U))) != 0U);
        if ((sat < 64U) && neg)
        {
            val = (uint64_t) (val | ~((1ULL << sat) - 1U));  // Sign extension
        }
        return neg ? (int64_t) ((-(int64_t) ~val) - 1) : (int64_t) val;
    }

    // ---------------------------------------------------- FLOAT16 ----------------------------------------------------

    DSDL_RUNTIME_STATIC_ASSERT(DSDL_RUNTIME_PLATFORM_IEEE754_FLOAT,
                               "The target platform does not support IEEE754 floating point operations.");
    DSDL_RUNTIME_STATIC_ASSERT(32U == (sizeof(float) * 8U), "Unsupported floating point model");

    /// @brief Converts a 32-bit float into IEEE-754 binary16 representation.
    /// @param[in] value Single-precision floating-point value.
    /// @return Packed IEEE-754 binary16 bit pattern.
    static inline uint16_t dsdl_runtime_float16_pack(const float value)
    {
        typedef union  // NOSONAR
        {
            uint32_t bits;
            float    real;
        } Float32Bits;

        // The no-lint statements suppress the warning about the use of union. This is required for low-level bit
        // access.
        const uint32_t round_mask = ~(uint32_t) 0x0FFFU;
        Float32Bits    f32inf;  // NOSONAR
        Float32Bits    f16inf;  // NOSONAR
        Float32Bits    magic;   // NOSONAR
        Float32Bits    in;      // NOSONAR
        f32inf.bits         = ((uint32_t) 255U) << 23U;
        f16inf.bits         = ((uint32_t) 31U) << 23U;
        magic.bits          = ((uint32_t) 15U) << 23U;
        in.real             = value;
        const uint32_t sign = in.bits & (((uint32_t) 1U) << 31U);
        in.bits ^= sign;
        uint16_t out = 0;
        if (in.bits >= f32inf.bits)
        {
            if ((in.bits & 0x7FFFFFUL) != 0)
            {
                out = 0x7E00U;
            }
            else
            {
                out = (in.bits > f32inf.bits) ? (uint16_t) 0x7FFFU : (uint16_t) 0x7C00U;
            }
        }
        else
        {
            in.bits &= round_mask;
            in.real *= magic.real;
            in.bits -= round_mask;
            if (in.bits > f16inf.bits)
            {
                in.bits = f16inf.bits;
            }
            out = (uint16_t) (in.bits >> 13U);
        }
        out |= (uint16_t) (sign >> 16U);
        return out;
    }

    /// @brief Converts an IEEE-754 binary16 value into 32-bit float.
    /// @param[in] value Packed IEEE-754 binary16 bit pattern.
    /// @return Single-precision floating-point value.
    static inline float dsdl_runtime_float16_unpack(const uint16_t value)
    {
        typedef union  // NOSONAR
        {
            uint32_t bits;
            float    real;
        } Float32Bits;

        // The no-lint statements suppress the warning about the use of union. This is required for low-level bit
        // access.
        Float32Bits magic;    // NOSONAR
        Float32Bits inf_nan;  // NOSONAR
        Float32Bits out;      // NOSONAR
        magic.bits   = ((uint32_t) 0xEFU) << 23U;
        inf_nan.bits = ((uint32_t) 0x8FU) << 23U;
        out.bits     = ((uint32_t) (value & 0x7FFFU)) << 13U;
        out.real *= magic.real;
        if (out.real >= inf_nan.real)
        {
            out.bits |= ((uint32_t) 0xFFU) << 23U;
        }
        out.bits |= ((uint32_t) (value & 0x8000U)) << 16U;
        return out.real;
    }

    /// @brief Serializes a binary16 floating-point value.
    /// @param[out] buf Destination serialized buffer.
    /// @param[in] buf_size_bytes Destination buffer size in bytes.
    /// @param[in] off_bits Destination bit offset.
    /// @param[in] value Single-precision value to convert and serialize.
    /// @return `DSDL_RUNTIME_SUCCESS` or a negative error code.
    static inline int8_t dsdl_runtime_set_f16(uint8_t* const buf,
                                              const size_t   buf_size_bytes,
                                              const size_t   off_bits,
                                              const float    value)
    {
        return dsdl_runtime_set_uxx(buf, buf_size_bytes, off_bits, dsdl_runtime_float16_pack(value), 16U);
    }

    /// @brief Deserializes a binary16 floating-point value.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @return Deserialized single-precision value.
    static inline float dsdl_runtime_get_f16(const uint8_t* const buf,
                                             const size_t         buf_size_bytes,
                                             const size_t         off_bits)
    {
        return dsdl_runtime_float16_unpack(dsdl_runtime_get_u16(buf, buf_size_bytes, off_bits, 16U));
    }

    // ---------------------------------------------------- FLOAT32 ----------------------------------------------------

    DSDL_RUNTIME_STATIC_ASSERT(DSDL_RUNTIME_PLATFORM_IEEE754_FLOAT,
                               "The target platform does not support IEEE754 floating point operations.");
    DSDL_RUNTIME_STATIC_ASSERT(32U == (sizeof(float) * 8U), "Unsupported floating point model");

    /// @brief Serializes a 32-bit IEEE-754 floating-point value.
    /// @param[out] buf Destination serialized buffer.
    /// @param[in] buf_size_bytes Destination buffer size in bytes.
    /// @param[in] off_bits Destination bit offset.
    /// @param[in] value Floating-point value to serialize.
    /// @return `DSDL_RUNTIME_SUCCESS` or a negative error code.
    static inline int8_t dsdl_runtime_set_f32(uint8_t* const buf,
                                              const size_t   buf_size_bytes,
                                              const size_t   off_bits,
                                              const float    value)
    {
        // Intentional violation of MISRA: use union to perform fast conversion from an IEEE 754-compatible native
        // representation into a serializable integer. The assumptions about the target platform properties are made
        // clear. In the future we may add a more generic conversion that is platform-invariant.
        union  // NOSONAR
        {
            float    fl;
            uint32_t in;
        } const tmp = {value};  // NOSONAR
        return dsdl_runtime_set_uxx(buf, buf_size_bytes, off_bits, tmp.in, sizeof(tmp) * 8U);
    }

    /// @brief Deserializes a 32-bit IEEE-754 floating-point value.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @return Deserialized floating-point value.
    static inline float dsdl_runtime_get_f32(const uint8_t* const buf,
                                             const size_t         buf_size_bytes,
                                             const size_t         off_bits)
    {
        // Intentional violation of MISRA: use union to perform fast conversion to an IEEE 754-compatible native
        // representation into a serializable integer. The assumptions about the target platform properties are made
        // clear. In the future we may add a more generic conversion that is platform-invariant.
        union  // NOSONAR
        {
            uint32_t in;
            float    fl;
        } const tmp = {dsdl_runtime_get_u32(buf, buf_size_bytes, off_bits, 32U)};
        return tmp.fl;
    }

    // ---------------------------------------------------- FLOAT64 ----------------------------------------------------

    DSDL_RUNTIME_STATIC_ASSERT(
        DSDL_RUNTIME_PLATFORM_IEEE754_DOUBLE,
        "The target platform does not support IEEE754 double-precision floating point operations.");
    DSDL_RUNTIME_STATIC_ASSERT(64U == (sizeof(double) * 8U), "Unsupported floating point model");

    /// @brief Serializes a 64-bit IEEE-754 floating-point value.
    /// @param[out] buf Destination serialized buffer.
    /// @param[in] buf_size_bytes Destination buffer size in bytes.
    /// @param[in] off_bits Destination bit offset.
    /// @param[in] value Floating-point value to serialize.
    /// @return `DSDL_RUNTIME_SUCCESS` or a negative error code.
    static inline int8_t dsdl_runtime_set_f64(uint8_t* const buf,
                                              const size_t   buf_size_bytes,
                                              const size_t   off_bits,
                                              const double   value)
    {
        // Intentional violation of MISRA: use union to perform fast conversion from an IEEE 754-compatible native
        // representation into a serializable integer. The assumptions about the target platform properties are made
        // clear. In the future we may add a more generic conversion that is platform-invariant.
        union  // NOSONAR
        {
            double   fl;
            uint64_t in;
        } const tmp = {value};  // NOSONAR
        return dsdl_runtime_set_uxx(buf, buf_size_bytes, off_bits, tmp.in, sizeof(tmp) * 8U);
    }

    /// @brief Deserializes a 64-bit IEEE-754 floating-point value.
    /// @param[in] buf Source serialized buffer.
    /// @param[in] buf_size_bytes Source buffer size in bytes.
    /// @param[in] off_bits Source bit offset.
    /// @return Deserialized floating-point value.
    static inline double dsdl_runtime_get_f64(const uint8_t* const buf,
                                              const size_t         buf_size_bytes,
                                              const size_t         off_bits)
    {
        // Intentional violation of MISRA: use union to perform fast conversion to an IEEE 754-compatible native
        // representation into a serializable integer. The assumptions about the target platform properties are made
        // clear. In the future we may add a more generic conversion that is platform-invariant.
        union  // NOSONAR
        {
            uint64_t in;
            double   fl;
        } const tmp = {dsdl_runtime_get_u64(buf, buf_size_bytes, off_bits, 64U)};
        return tmp.fl;
    }

#ifdef __cplusplus
}
#endif

#endif  // LLVMDSDL_RUNTIME_DSDL_RUNTIME_H
