//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Differential parity harness for signed-narrow C++-vs-C bindings.
///
/// The test checks randomized deserialize/serialize parity for saturating and
/// truncating 3-bit integer fixtures generated for C and C++.
///
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

extern "C"
{
#include "vendor/Int3Sat_1_0.h"
#include "vendor/Int3Trunc_1_0.h"
}

#include "vendor/Int3Sat_1_0.hpp"
#include "vendor/Int3Trunc_1_0.hpp"

namespace
{

std::uint64_t gRngState = UINT64_C(0xE6A1FD3BC9157A2D);

std::uint32_t nextRandomU32()
{
    gRngState ^= gRngState << 13U;
    gRngState ^= gRngState >> 7U;
    gRngState ^= gRngState << 17U;
    return static_cast<std::uint32_t>(gRngState & UINT64_C(0xFFFFFFFF));
}

void fillRandomBytes(std::uint8_t* const dst, const std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        dst[i] = static_cast<std::uint8_t>(nextRandomU32() & UINT32_C(0xFF));
    }
}

template <typename CObj, typename CppObj>
int runCase(const char* const name,
            const std::size_t iterations,
            const std::size_t cMaxSize,
            const std::size_t cppMaxSize,
            std::int8_t (*cDeserialize)(CObj*, const std::uint8_t*, std::size_t*),
            std::int8_t (*cSerialize)(const CObj*, std::uint8_t*, std::size_t*),
            std::int8_t (*cppDeserialize)(CppObj*, const std::uint8_t*, std::size_t*),
            std::int8_t (*cppSerialize)(const CppObj*, std::uint8_t*, std::size_t*))
{
    static_assert(std::is_trivially_copyable<CObj>::value, "C object must be trivially copyable");
    constexpr std::size_t kMaxIoBuffer = 32U;
    std::uint8_t          input[kMaxIoBuffer];
    std::uint8_t          cOutput[kMaxIoBuffer];
    std::uint8_t          cppOutput[kMaxIoBuffer];
    const std::size_t     maxSerialized = (cMaxSize > cppMaxSize) ? cMaxSize : cppMaxSize;
    const std::size_t     maxInput      = maxSerialized + 8U;

    for (std::size_t iter = 0; iter < iterations; ++iter)
    {
        const std::size_t inputSize =
            static_cast<std::size_t>(nextRandomU32() % static_cast<std::uint32_t>(maxInput + 1U));
        fillRandomBytes(input, inputSize);

        CObj cObj{};
        std::memset(&cObj, 0, sizeof(cObj));
        CppObj cppObj{};

        std::size_t       cConsumed    = inputSize;
        std::size_t       cppConsumed  = inputSize;
        const std::int8_t cDesResult   = cDeserialize(&cObj, input, &cConsumed);
        const std::int8_t cppDesResult = cppDeserialize(&cppObj, input, &cppConsumed);
        if ((cDesResult != cppDesResult) || (cConsumed != cppConsumed))
        {
            std::fprintf(stderr,
                         "Deserialize mismatch in %s iter=%zu C(rc=%d,consumed=%zu) "
                         "C++(rc=%d,consumed=%zu)\n",
                         name,
                         iter,
                         static_cast<int>(cDesResult),
                         cConsumed,
                         static_cast<int>(cppDesResult),
                         cppConsumed);
            return 1;
        }
        if (cDesResult < 0)
        {
            continue;
        }

        std::memset(cOutput, 0xA5, sizeof(cOutput));
        std::memset(cppOutput, 0xA5, sizeof(cppOutput));
        std::size_t       cSize        = maxSerialized;
        std::size_t       cppSize      = maxSerialized;
        const std::int8_t cSerResult   = cSerialize(&cObj, cOutput, &cSize);
        const std::int8_t cppSerResult = cppSerialize(&cppObj, cppOutput, &cppSize);
        const bool        byteMismatch = (cSize == cppSize) && (std::memcmp(cOutput, cppOutput, cSize) != 0);
        if ((cSerResult != cppSerResult) || (cSize != cppSize) || byteMismatch)
        {
            std::fprintf(stderr,
                         "Serialize mismatch in %s iter=%zu C(rc=%d,size=%zu) C++(rc=%d,size=%zu)\n",
                         name,
                         iter,
                         static_cast<int>(cSerResult),
                         cSize,
                         static_cast<int>(cppSerResult),
                         cppSize);
            return 1;
        }
    }

    std::printf("PASS %s random (%zu iterations)\n", name, iterations);
    return 0;
}

std::int8_t cInt3SatDeserialize(vendor__Int3Sat@CV1_0@* outObj, const std::uint8_t* buffer, std::size_t* inoutSize)
{
    return vendor__Int3Sat@CV1_0@__deserialize_(outObj, buffer, inoutSize);
}
std::int8_t cInt3SatSerialize(const vendor__Int3Sat@CV1_0@* obj, std::uint8_t* buffer, std::size_t* inoutSize)
{
    return vendor__Int3Sat@CV1_0@__serialize_(obj, buffer, inoutSize);
}
std::int8_t cppInt3SatDeserialize(vendor::Int3Sat@CV1_0@* outObj, const std::uint8_t* buffer, std::size_t* inoutSize)
{
    return outObj->deserialize(buffer, inoutSize);
}
std::int8_t cppInt3SatSerialize(const vendor::Int3Sat@CV1_0@* obj, std::uint8_t* buffer, std::size_t* inoutSize)
{
    return obj->serialize(buffer, inoutSize);
}

std::int8_t cInt3TruncDeserialize(vendor__Int3Trunc@CV1_0@* outObj, const std::uint8_t* buffer, std::size_t* inoutSize)
{
    return vendor__Int3Trunc@CV1_0@__deserialize_(outObj, buffer, inoutSize);
}
std::int8_t cInt3TruncSerialize(const vendor__Int3Trunc@CV1_0@* obj, std::uint8_t* buffer, std::size_t* inoutSize)
{
    return vendor__Int3Trunc@CV1_0@__serialize_(obj, buffer, inoutSize);
}
std::int8_t cppInt3TruncDeserialize(vendor::Int3Trunc@CV1_0@* outObj, const std::uint8_t* buffer, std::size_t* inoutSize)
{
    return outObj->deserialize(buffer, inoutSize);
}
std::int8_t cppInt3TruncSerialize(const vendor::Int3Trunc@CV1_0@* obj, std::uint8_t* buffer, std::size_t* inoutSize)
{
    return obj->serialize(buffer, inoutSize);
}

int runDirectedChecks()
{
    {
        // Saturated int3: +7 -> +3 (0b011).
        vendor__Int3Sat@CV1_0@ cObj{};
        vendor::Int3Sat@CV1_0@ cppObj{};
        cObj.value   = static_cast<std::int8_t>(7);
        cppObj.value = static_cast<std::int8_t>(7);
        std::uint8_t      cBuffer[8]{};
        std::uint8_t      cppBuffer[8]{};
        std::size_t       cSize   = static_cast<std::size_t>(vendor__Int3Sat@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize = vendor::Int3Sat@CV1_0@::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cRc     = cInt3SatSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc   = cppInt3SatSerialize(&cppObj, cppBuffer, &cppSize);
        if ((cRc != 0) || (cppRc != 0) || (cSize != 1U) || (cppSize != 1U) || (cBuffer[0] != cppBuffer[0]) ||
            (cBuffer[0] != 0x03U))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Int3Sat +7 serialize): "
                         "C(rc=%d,size=%zu,byte=%02X) C++(rc=%d,size=%zu,byte=%02X)\n",
                         static_cast<int>(cRc),
                         cSize,
                         cBuffer[0],
                         static_cast<int>(cppRc),
                         cppSize,
                         cppBuffer[0]);
            return 1;
        }
        std::printf("INFO signed-narrow-cpp-c directed marker int3sat_serialize_plus7_saturated\n");
    }

    {
        // Saturated int3: -9 -> -4 (0b100).
        vendor__Int3Sat@CV1_0@ cObj{};
        vendor::Int3Sat@CV1_0@ cppObj{};
        cObj.value   = static_cast<std::int8_t>(-9);
        cppObj.value = static_cast<std::int8_t>(-9);
        std::uint8_t      cBuffer[8]{};
        std::uint8_t      cppBuffer[8]{};
        std::size_t       cSize   = static_cast<std::size_t>(vendor__Int3Sat@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize = vendor::Int3Sat@CV1_0@::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cRc     = cInt3SatSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc   = cppInt3SatSerialize(&cppObj, cppBuffer, &cppSize);
        if ((cRc != 0) || (cppRc != 0) || (cSize != 1U) || (cppSize != 1U) || (cBuffer[0] != cppBuffer[0]) ||
            (cBuffer[0] != 0x04U))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Int3Sat -9 serialize): "
                         "C(rc=%d,size=%zu,byte=%02X) C++(rc=%d,size=%zu,byte=%02X)\n",
                         static_cast<int>(cRc),
                         cSize,
                         cBuffer[0],
                         static_cast<int>(cppRc),
                         cppSize,
                         cppBuffer[0]);
            return 1;
        }
        std::printf("INFO signed-narrow-cpp-c directed marker int3sat_serialize_minus9_saturated\n");
    }

    {
        // Truncated int3: +5 -> 0b101 (decode -3).
        vendor__Int3Trunc@CV1_0@ cObj{};
        vendor::Int3Trunc@CV1_0@ cppObj{};
        cObj.value   = static_cast<std::int8_t>(5);
        cppObj.value = static_cast<std::int8_t>(5);
        std::uint8_t      cBuffer[8]{};
        std::uint8_t      cppBuffer[8]{};
        std::size_t       cSize   = static_cast<std::size_t>(vendor__Int3Trunc@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize = vendor::Int3Trunc@CV1_0@::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cRc     = cInt3TruncSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc   = cppInt3TruncSerialize(&cppObj, cppBuffer, &cppSize);
        if ((cRc != 0) || (cppRc != 0) || (cSize != 1U) || (cppSize != 1U) || (cBuffer[0] != cppBuffer[0]) ||
            (cBuffer[0] != 0x05U))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Int3Trunc +5 serialize): "
                         "C(rc=%d,size=%zu,byte=%02X) C++(rc=%d,size=%zu,byte=%02X)\n",
                         static_cast<int>(cRc),
                         cSize,
                         cBuffer[0],
                         static_cast<int>(cppRc),
                         cppSize,
                         cppBuffer[0]);
            return 1;
        }
        std::printf("INFO signed-narrow-cpp-c directed marker int3trunc_serialize_plus5_truncated\n");
    }

    {
        // Truncated int3: -5 -> low bits 0b011.
        vendor__Int3Trunc@CV1_0@ cObj{};
        vendor::Int3Trunc@CV1_0@ cppObj{};
        cObj.value   = static_cast<std::int8_t>(-5);
        cppObj.value = static_cast<std::int8_t>(-5);
        std::uint8_t      cBuffer[8]{};
        std::uint8_t      cppBuffer[8]{};
        std::size_t       cSize   = static_cast<std::size_t>(vendor__Int3Trunc@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_);
        std::size_t       cppSize = vendor::Int3Trunc@CV1_0@::SERIALIZATION_BUFFER_SIZE_BYTES;
        const std::int8_t cRc     = cInt3TruncSerialize(&cObj, cBuffer, &cSize);
        const std::int8_t cppRc   = cppInt3TruncSerialize(&cppObj, cppBuffer, &cppSize);
        if ((cRc != 0) || (cppRc != 0) || (cSize != 1U) || (cppSize != 1U) || (cBuffer[0] != cppBuffer[0]) ||
            (cBuffer[0] != 0x03U))
        {
            std::fprintf(stderr,
                         "Directed mismatch (Int3Trunc -5 serialize): "
                         "C(rc=%d,size=%zu,byte=%02X) C++(rc=%d,size=%zu,byte=%02X)\n",
                         static_cast<int>(cRc),
                         cSize,
                         cBuffer[0],
                         static_cast<int>(cppRc),
                         cppSize,
                         cppBuffer[0]);
            return 1;
        }
        std::printf("INFO signed-narrow-cpp-c directed marker int3trunc_serialize_minus5_truncated\n");
    }

    {
        // Sign extension in deserialize path: 0b111 -> -1, 0b100 -> -4.
        for (const auto sample : {std::uint8_t{0x07U}, std::uint8_t{0x04U}})
        {
            vendor__Int3Sat@CV1_0@   cObj{};
            vendor::Int3Sat@CV1_0@   cppObj{};
            std::size_t       cConsumed   = 1U;
            std::size_t       cppConsumed = 1U;
            const std::int8_t cRc         = cInt3SatDeserialize(&cObj, &sample, &cConsumed);
            const std::int8_t cppRc       = cppInt3SatDeserialize(&cppObj, &sample, &cppConsumed);
            if ((cRc != 0) || (cppRc != 0) || (cConsumed != cppConsumed) || (cObj.value != cppObj.value))
            {
                std::fprintf(stderr, "Directed mismatch (Int3Sat deserialize sign extension sample=%02X)\n", sample);
                return 1;
            }
            const std::int8_t expected =
                (sample == 0x07U) ? static_cast<std::int8_t>(-1) : static_cast<std::int8_t>(-4);
            if (cObj.value != expected)
            {
                std::fprintf(stderr,
                             "Directed mismatch (Int3Sat deserialize expected=%d got=%d sample=%02X)\n",
                             static_cast<int>(expected),
                             static_cast<int>(cObj.value),
                             sample);
                return 1;
            }
            if (sample == 0x07U)
            {
                std::printf("INFO signed-narrow-cpp-c directed marker int3sat_sign_extend_0x07\n");
            }
            else
            {
                std::printf("INFO signed-narrow-cpp-c directed marker int3sat_sign_extend_0x04\n");
            }
        }
    }

    {
        // Truncated deserialize sign extension: 0b101 -> -3, 0b011 -> +3.
        for (const auto sample : {std::uint8_t{0x05U}, std::uint8_t{0x03U}})
        {
            vendor__Int3Trunc@CV1_0@ cObj{};
            vendor::Int3Trunc@CV1_0@ cppObj{};
            std::size_t       cConsumed   = 1U;
            std::size_t       cppConsumed = 1U;
            const std::int8_t cRc         = cInt3TruncDeserialize(&cObj, &sample, &cConsumed);
            const std::int8_t cppRc       = cppInt3TruncDeserialize(&cppObj, &sample, &cppConsumed);
            if ((cRc != 0) || (cppRc != 0) || (cConsumed != cppConsumed) || (cObj.value != cppObj.value))
            {
                std::fprintf(stderr, "Directed mismatch (Int3Trunc deserialize sign extension sample=%02X)\n", sample);
                return 1;
            }
            const std::int8_t expected = (sample == 0x05U) ? static_cast<std::int8_t>(-3) : static_cast<std::int8_t>(3);
            if (cObj.value != expected)
            {
                std::fprintf(stderr,
                             "Directed mismatch (Int3Trunc deserialize expected=%d got=%d sample=%02X)\n",
                             static_cast<int>(expected),
                             static_cast<int>(cObj.value),
                             sample);
                return 1;
            }
            if (sample == 0x05U)
            {
                std::printf("INFO signed-narrow-cpp-c directed marker int3trunc_sign_extend_0x05\n");
            }
            else
            {
                std::printf("INFO signed-narrow-cpp-c directed marker int3trunc_sign_extend_0x03\n");
            }
        }
    }

    std::printf("PASS signed_narrow_directed_cpp_c directed\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    constexpr std::size_t kRandomCases   = 2U;
    constexpr std::size_t kDirectedCases = 1U;
    std::size_t           iterations     = 256U;
    if (argc > 1)
    {
        char*               endptr = nullptr;
        const unsigned long parsed = std::strtoul(argv[1], &endptr, 10);
        if ((endptr == nullptr) || (*endptr != '\0') || (parsed == 0UL))
        {
            std::fprintf(stderr, "Invalid iteration count: %s\n", argv[1]);
            return 2;
        }
        iterations = static_cast<std::size_t>(parsed);
    }

    if (runCase<vendor__Int3Sat@CV1_0@, vendor::Int3Sat@CV1_0@>("vendor.Int3Sat.1.0",
                                                  iterations,
                                                  static_cast<std::size_t>(
                                                      vendor__Int3Sat@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_),
                                                  vendor::Int3Sat@CV1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
                                                  cInt3SatDeserialize,
                                                  cInt3SatSerialize,
                                                  cppInt3SatDeserialize,
                                                  cppInt3SatSerialize) != 0)
    {
        return 1;
    }

    if (runCase<vendor__Int3Trunc@CV1_0@, vendor::Int3Trunc@CV1_0@>("vendor.Int3Trunc.1.0",
                                                      iterations,
                                                      static_cast<std::size_t>(
                                                          vendor__Int3Trunc@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_),
                                                      vendor::Int3Trunc@CV1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
                                                      cInt3TruncDeserialize,
                                                      cInt3TruncSerialize,
                                                      cppInt3TruncDeserialize,
                                                      cppInt3TruncSerialize) != 0)
    {
        return 1;
    }

    if (runDirectedChecks() != 0)
    {
        return 1;
    }

    std::printf("PASS signed-narrow-cpp-c inventory random_cases=%zu directed_cases=%zu\n",
                kRandomCases,
                kDirectedCases);
    std::printf("PASS signed-narrow-cpp-c parity random_iterations=%zu random_cases=%zu directed_cases=%zu\n",
                iterations,
                kRandomCases,
                kDirectedCases);
    std::printf("Signed narrow C/C++ parity PASS\n");
    return 0;
}
