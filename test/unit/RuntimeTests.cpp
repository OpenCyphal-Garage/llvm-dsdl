//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <iostream>

#include "dsdl_runtime.h"

#include "UnitTests.h"

bool runRuntimeTests()
{
    {
        if (DSDL_RUNTIME_SUCCESS != 0 || DSDL_RUNTIME_ERROR_INVALID_ARGUMENT != 2 ||
            DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL != 3 ||
            DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH != 10 ||
            DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG != 11 ||
            DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER != 12)
        {
            std::cerr << "runtime error code constants mismatch\n";
            return false;
        }
    }

    {
        std::uint8_t buffer[2] = {0U, 0U};
        const auto   err       = dsdl_runtime_set_uxx(buffer, 2U, 3U, 0x15U, 5U);
        if (err < 0)
        {
            std::cerr << "dsdl_runtime_set_uxx failed unexpectedly\n";
            return false;
        }
        const auto got = dsdl_runtime_get_u8(buffer, 2U, 3U, 5U);
        if (got != 0x15U)
        {
            std::cerr << "unaligned get/set mismatch\n";
            return false;
        }
    }

    {
        const std::uint8_t src[1] = {0xFFU};
        std::uint8_t       out[2] = {0xAAU, 0xAAU};
        dsdl_runtime_get_bits(out, src, 1U, 4U, 12U);
        if (out[1] != 0U)
        {
            std::cerr << "implicit zero-extension did not clear tail bytes\n";
            return false;
        }
    }

    {
        constexpr float kValue   = 1.5F;
        const auto      packed   = dsdl_runtime_float16_pack(kValue);
        const auto      unpacked = dsdl_runtime_float16_unpack(packed);
        if (std::fabs(unpacked - kValue) > 0.01F)
        {
            std::cerr << "float16 pack/unpack mismatch\n";
            return false;
        }
    }

    {
        std::uint8_t buffer[1] = {0U};
        const auto   err       = dsdl_runtime_set_uxx(buffer, 1U, 7U, 0x3U, 2U);
        if (err != -static_cast<std::int8_t>(DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL))
        {
            std::cerr << "buffer-too-small path returned unexpected error\n";
            return false;
        }
    }

    return true;
}
