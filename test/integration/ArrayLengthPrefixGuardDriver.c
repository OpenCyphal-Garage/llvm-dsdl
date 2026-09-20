//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Array-length prefixes decoded by the generated C: above the capacity, beyond the width of
// size_t, and at the capacity. One line per case and a summary line the lane parses.
//
//===----------------------------------------------------------------------===//

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsdl_runtime.h"
#include "prefixguard/Prefix32_1_0.h"
#include "prefixguard/Prefix64_1_0.h"

// clang-format off
typedef struct prefixguard__Prefix32@CV1_0@ Prefix32;
typedef struct prefixguard__Prefix64@CV1_0@ Prefix64;
#define PREFIX32_DESERIALIZE prefixguard__Prefix32@CV1_0@__deserialize_
#define PREFIX64_DESERIALIZE prefixguard__Prefix64@CV1_0@__deserialize_
#define PREFIX32_SERIALIZATION_BUFFER_SIZE_BYTES prefixguard__Prefix32@CV1_0@_SERIALIZATION_BUFFER_SIZE_BYTES_
// clang-format on

static const int8_t kBadArrayLength = -DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;

// Prefix64 embeds its bit-packed capacity, 1 GiB, so one static object serves every case rather
// than an allocation per probe: a rejected decode never touches the array, and the accepting case
// writes one byte, which the next case clears.
static Prefix32 g_prefix32;
static Prefix64 g_prefix64;

static Prefix32* fresh_prefix32(void)
{
    g_prefix32.payload.count = 0U;
    return &g_prefix32;
}

static Prefix64* fresh_prefix64(void)
{
    g_prefix64.flags.count        = 0U;
    g_prefix64.flags.bitpacked[0] = 0U;
    return &g_prefix64;
}

static unsigned g_passed  = 0U;
static unsigned g_skipped = 0U;
static unsigned g_failed  = 0U;

static void outcome(const char* const verdict, const char* const name, const uint64_t prefix, const char* const detail)
{
    if (strcmp(verdict, "PASS") == 0)
    {
        ++g_passed;
    }
    else if (strcmp(verdict, "SKIP") == 0)
    {
        ++g_skipped;
    }
    else
    {
        ++g_failed;
    }
    if (detail == NULL)
    {
        printf("%s %s prefix=%" PRIu64 "\n", verdict, name, prefix);
    }
    else if (strcmp(verdict, "FAIL") == 0)
    {
        printf("%s %s prefix=%" PRIu64 ": %s\n", verdict, name, prefix, detail);
    }
    else
    {
        printf("%s %s prefix=%" PRIu64 " %s\n", verdict, name, prefix, detail);
    }
}

// A buffer opening with a little-endian prefix of prefix_bytes bytes followed by payload_bytes zero
// bytes.
static uint8_t* with_prefix(const uint64_t prefix, const size_t prefix_bytes, const size_t payload_bytes)
{
    uint8_t* const buffer = calloc(prefix_bytes + payload_bytes, 1U);
    if (buffer == NULL)
    {
        abort();
    }
    for (size_t i = 0; i < prefix_bytes; ++i)
    {
        buffer[i] = (uint8_t) (prefix >> (8U * i));
    }
    return buffer;
}

// A decode that has to fail with a bad array length and leave the array empty.
static void expect_rejected(const char* const name, const uint64_t prefix, const int8_t rc, const size_t count_after)
{
    char detail[128];
    if (rc != kBadArrayLength)
    {
        snprintf(detail, sizeof detail, "rc = %d, want %d", rc, kBadArrayLength);
        outcome("FAIL", name, prefix, detail);
        return;
    }
    if (count_after != 0U)
    {
        snprintf(detail, sizeof detail, "array holds %zu elements after rejection", count_after);
        outcome("FAIL", name, prefix, detail);
        return;
    }
    outcome("PASS", name, prefix, NULL);
}

static void prefix32_rejects_above_capacity(const uint32_t prefix)
{
    Prefix32* const obj    = fresh_prefix32();
    uint8_t* const  buffer = with_prefix(prefix, 4U, 0U);
    size_t          size   = 4U;
    const int8_t    rc     = PREFIX32_DESERIALIZE(obj, buffer, &size);
    expect_rejected("prefix32_rejects_above_capacity", prefix, rc, obj->payload.count);
    free(buffer);
}

static void prefix32_accepts_capacity(void)
{
    const size_t    capacity = 65536U;
    Prefix32* const obj      = fresh_prefix32();
    uint8_t* const  buffer   = with_prefix(capacity, 4U, capacity);
    size_t          size     = 4U + capacity;
    const int8_t    rc       = PREFIX32_DESERIALIZE(obj, buffer, &size);
    char            detail[128];
    if (rc != 0)
    {
        snprintf(detail, sizeof detail, "rc = %d, want success", rc);
        outcome("FAIL", "prefix32_accepts_capacity", capacity, detail);
    }
    else if (size != PREFIX32_SERIALIZATION_BUFFER_SIZE_BYTES)
    {
        snprintf(detail,
                 sizeof detail,
                 "consumed %zu bytes, want %zu",
                 size,
                 (size_t) PREFIX32_SERIALIZATION_BUFFER_SIZE_BYTES);
        outcome("FAIL", "prefix32_accepts_capacity", capacity, detail);
    }
    else if (obj->payload.count != capacity)
    {
        snprintf(detail, sizeof detail, "payload holds %zu elements, want %zu", obj->payload.count, capacity);
        outcome("FAIL", "prefix32_accepts_capacity", capacity, detail);
    }
    else
    {
        outcome("PASS", "prefix32_accepts_capacity", capacity, NULL);
    }
    free(buffer);
}

static void prefix64_rejects_above_capacity(const uint64_t prefix)
{
    Prefix64* const obj    = fresh_prefix64();
    uint8_t* const  buffer = with_prefix(prefix, 8U, 0U);
    size_t          size   = 8U;
    const int8_t    rc     = PREFIX64_DESERIALIZE(obj, buffer, &size);
    expect_rejected("prefix64_rejects_above_capacity", prefix, rc, obj->flags.count);
    free(buffer);
}

// A length within the type's capacity that size_t cannot hold is rejected as a bad array length.
// A 64-bit size_t holds every length this type allows, so the case is skipped there.
static void prefix64_rejects_beyond_index(const uint64_t prefix)
{
    if (SIZE_MAX >= (UINT64_C(1) << 33U))
    {
        outcome("SKIP", "prefix64_rejects_beyond_index", prefix, "size_t holds every length Prefix64 allows");
        return;
    }
    Prefix64* const obj    = fresh_prefix64();
    uint8_t* const  buffer = with_prefix(prefix, 8U, 0U);
    size_t          size   = 8U;
    const int8_t    rc     = PREFIX64_DESERIALIZE(obj, buffer, &size);
    expect_rejected("prefix64_rejects_beyond_index", prefix, rc, obj->flags.count);
    free(buffer);
}

static void prefix64_accepts_small_length(void)
{
    Prefix64* const obj    = fresh_prefix64();
    uint8_t* const  buffer = with_prefix(3U, 8U, 1U);
    size_t          size   = 9U;
    char            detail[128];
    buffer[8]       = 0x05U;
    const int8_t rc = PREFIX64_DESERIALIZE(obj, buffer, &size);
    if (rc != 0)
    {
        snprintf(detail, sizeof detail, "rc = %d, want success", rc);
        outcome("FAIL", "prefix64_accepts_small_length", 3U, detail);
    }
    else if (size != 9U)
    {
        snprintf(detail, sizeof detail, "consumed %zu bytes, want 9", size);
        outcome("FAIL", "prefix64_accepts_small_length", 3U, detail);
    }
    else if (obj->flags.count != 3U || (obj->flags.bitpacked[0] & 0x07U) != 0x05U)
    {
        snprintf(detail,
                 sizeof detail,
                 "flags count %zu bits 0x%02x, want 3 elements holding true, false, true",
                 obj->flags.count,
                 (unsigned) obj->flags.bitpacked[0]);
        outcome("FAIL", "prefix64_accepts_small_length", 3U, detail);
    }
    else
    {
        outcome("PASS", "prefix64_accepts_small_length", 3U, NULL);
    }
    free(buffer);
}

int main(void)
{
    static const uint32_t narrow_prefixes[] = {65537U, UINT32_C(1) << 31U, UINT32_MAX};
    static const uint64_t wide_prefixes[]   = {(UINT64_C(1) << 33U) + 1U,
                                               (UINT64_C(1) << 33U) + 3U,
                                               UINT64_C(1) << 63U,
                                               UINT64_MAX};
    static const uint64_t index_prefixes[]  = {UINT64_C(1) << 32U, (UINT64_C(1) << 32U) + 3U, UINT64_C(1) << 33U};

    for (size_t i = 0; i < sizeof narrow_prefixes / sizeof narrow_prefixes[0]; ++i)
    {
        prefix32_rejects_above_capacity(narrow_prefixes[i]);
    }
    prefix32_accepts_capacity();
    for (size_t i = 0; i < sizeof wide_prefixes / sizeof wide_prefixes[0]; ++i)
    {
        prefix64_rejects_above_capacity(wide_prefixes[i]);
    }
    for (size_t i = 0; i < sizeof index_prefixes / sizeof index_prefixes[0]; ++i)
    {
        prefix64_rejects_beyond_index(index_prefixes[i]);
    }
    prefix64_accepts_small_length();

    const unsigned cases = g_passed + g_skipped + g_failed;
    printf("%s c-array-length-prefix-guard cases=%u passed=%u skipped=%u failed=%u\n",
           g_failed == 0U ? "PASS" : "FAIL",
           cases,
           g_passed,
           g_skipped,
           g_failed);
    return g_failed == 0U ? 0 : 1;
}
