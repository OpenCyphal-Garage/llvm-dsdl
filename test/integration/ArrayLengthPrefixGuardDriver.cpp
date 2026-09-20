//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Array-length prefixes decoded by the generated C++: above the capacity, beyond the width of
// std::size_t, and at the capacity. One line per case and a summary line the lane parses.
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dsdl_runtime.h"
#include "prefixguard/Prefix32_1_0.hpp"
#include "prefixguard/Prefix64_1_0.hpp"

namespace
{

// clang-format off
using Prefix32 = prefixguard::Prefix32@V1_0@;
using Prefix64 = prefixguard::Prefix64@V1_0@;
// clang-format on

constexpr std::int8_t kBadArrayLength = -DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;

// The declared capacities survive the header on every target, a 32-bit std::size_t included.
static_assert(Prefix32::PAYLOAD_ARRAY_CAPACITY == 65536ULL, "Prefix32 declares uint8[<=65536]");
static_assert(Prefix64::FLAGS_ARRAY_CAPACITY == 8589934592ULL, "Prefix64 declares bool[<=8589934592]");

unsigned g_passed  = 0U;
unsigned g_skipped = 0U;
unsigned g_failed  = 0U;

void outcome(const char* const verdict, const char* const name, const std::uint64_t prefix, const std::string& detail)
{
    if (std::strcmp(verdict, "PASS") == 0)
    {
        ++g_passed;
    }
    else if (std::strcmp(verdict, "SKIP") == 0)
    {
        ++g_skipped;
    }
    else
    {
        ++g_failed;
    }
    if (detail.empty())
    {
        std::printf("%s %s prefix=%llu\n", verdict, name, static_cast<unsigned long long>(prefix));
    }
    else if (std::strcmp(verdict, "FAIL") == 0)
    {
        std::printf("%s %s prefix=%llu: %s\n", verdict, name, static_cast<unsigned long long>(prefix), detail.c_str());
    }
    else
    {
        std::printf("%s %s prefix=%llu %s\n", verdict, name, static_cast<unsigned long long>(prefix), detail.c_str());
    }
}

// A buffer opening with a little-endian prefix of prefix_bytes bytes followed by payload_bytes zero
// bytes.
std::vector<std::uint8_t> with_prefix(const std::uint64_t prefix,
                                      const std::size_t   prefix_bytes,
                                      const std::size_t   payload_bytes)
{
    std::vector<std::uint8_t> buffer(prefix_bytes + payload_bytes, 0U);
    for (std::size_t i = 0; i < prefix_bytes; ++i)
    {
        buffer[i] = static_cast<std::uint8_t>(prefix >> (8U * i));
    }
    return buffer;
}

// A decode that has to fail with a bad array length and leave the array empty.
void expect_rejected(const char* const   name,
                     const std::uint64_t prefix,
                     const std::int8_t   rc,
                     const std::size_t   count_after)
{
    if (rc != kBadArrayLength)
    {
        outcome("FAIL", name, prefix, "rc = " + std::to_string(rc) + ", want " + std::to_string(kBadArrayLength));
        return;
    }
    if (count_after != 0U)
    {
        outcome("FAIL", name, prefix, "array holds " + std::to_string(count_after) + " elements after rejection");
        return;
    }
    outcome("PASS", name, prefix, "");
}

void prefix32_rejects_above_capacity(const std::uint32_t prefix)
{
    Prefix32   obj;
    const auto buffer = with_prefix(prefix, 4U, 0U);
    auto       size   = buffer.size();
    const auto rc     = obj.deserialize(buffer.data(), &size);
    expect_rejected("prefix32_rejects_above_capacity", prefix, rc, obj.payload.size());
}

void prefix32_accepts_capacity()
{
    constexpr std::size_t capacity = 65536U;
    Prefix32              obj;
    const auto            buffer = with_prefix(capacity, 4U, capacity);
    auto                  size   = buffer.size();
    const auto            rc     = obj.deserialize(buffer.data(), &size);
    if (rc != 0)
    {
        outcome("FAIL", "prefix32_accepts_capacity", capacity, "rc = " + std::to_string(rc) + ", want success");
    }
    else if (size != Prefix32::SERIALIZATION_BUFFER_SIZE_BYTES)
    {
        outcome("FAIL",
                "prefix32_accepts_capacity",
                capacity,
                "consumed " + std::to_string(size) + " bytes, want " +
                    std::to_string(Prefix32::SERIALIZATION_BUFFER_SIZE_BYTES));
    }
    else if (obj.payload.size() != capacity)
    {
        outcome("FAIL",
                "prefix32_accepts_capacity",
                capacity,
                "payload holds " + std::to_string(obj.payload.size()) + " elements, want " + std::to_string(capacity));
    }
    else
    {
        outcome("PASS", "prefix32_accepts_capacity", capacity, "");
    }
}

void prefix64_rejects_above_capacity(const std::uint64_t prefix)
{
    Prefix64   obj;
    const auto buffer = with_prefix(prefix, 8U, 0U);
    auto       size   = buffer.size();
    const auto rc     = obj.deserialize(buffer.data(), &size);
    expect_rejected("prefix64_rejects_above_capacity", prefix, rc, obj.flags.size());
}

// A length within the type's capacity that std::size_t cannot hold is rejected as a bad array
// length. A 64-bit std::size_t holds every length this type allows, so the case is skipped there.
void prefix64_rejects_beyond_index(const std::uint64_t prefix)
{
    if (sizeof(std::size_t) >= 8U)
    {
        outcome("SKIP", "prefix64_rejects_beyond_index", prefix, "std::size_t holds every length Prefix64 allows");
        return;
    }
    Prefix64   obj;
    const auto buffer = with_prefix(prefix, 8U, 0U);
    auto       size   = buffer.size();
    const auto rc     = obj.deserialize(buffer.data(), &size);
    expect_rejected("prefix64_rejects_beyond_index", prefix, rc, obj.flags.size());
}

void prefix64_accepts_small_length()
{
    Prefix64 obj;
    auto     buffer = with_prefix(3U, 8U, 1U);
    buffer[8]       = 0x05U;
    auto       size = buffer.size();
    const auto rc   = obj.deserialize(buffer.data(), &size);
    if (rc != 0)
    {
        outcome("FAIL", "prefix64_accepts_small_length", 3U, "rc = " + std::to_string(rc) + ", want success");
    }
    else if (size != buffer.size())
    {
        outcome("FAIL",
                "prefix64_accepts_small_length",
                3U,
                "consumed " + std::to_string(size) + " bytes, want " + std::to_string(buffer.size()));
    }
    else if (obj.flags.size() != 3U || !obj.flags[0] || obj.flags[1] || !obj.flags[2])
    {
        outcome("FAIL",
                "prefix64_accepts_small_length",
                3U,
                "flags hold " + std::to_string(obj.flags.size()) + " elements, want true, false, true");
    }
    else
    {
        outcome("PASS", "prefix64_accepts_small_length", 3U, "");
    }
}

}  // namespace

int main()
{
    for (const std::uint32_t prefix : {65537U, 1U << 31U, UINT32_MAX})
    {
        prefix32_rejects_above_capacity(prefix);
    }
    prefix32_accepts_capacity();
    for (const std::uint64_t prefix :
         {(UINT64_C(1) << 33U) + 1U, (UINT64_C(1) << 33U) + 3U, UINT64_C(1) << 63U, UINT64_MAX})
    {
        prefix64_rejects_above_capacity(prefix);
    }
    for (const std::uint64_t prefix : {UINT64_C(1) << 32U, (UINT64_C(1) << 32U) + 3U, UINT64_C(1) << 33U})
    {
        prefix64_rejects_beyond_index(prefix);
    }
    prefix64_accepts_small_length();

    const unsigned cases = g_passed + g_skipped + g_failed;
    std::printf("%s cpp-array-length-prefix-guard cases=%u passed=%u skipped=%u failed=%u\n",
                g_failed == 0U ? "PASS" : "FAIL",
                cases,
                g_passed,
                g_skipped,
                g_failed);
    return g_failed == 0U ? 0 : 1;
}
