//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Isolated cross-language runtime-primitive equivalence driver (C).
//
// Reads the shared vector file (argv[1]) and exercises each vector directly
// against the C runtime primitives, checking every primitive direction on its
// own so paired bugs cannot mask each other. Prints "PROCESSED <n>" on success
// and exits non-zero on the first mismatch. The Rust and Go drivers consume the
// exact same file, so any cross-language divergence fails one of them.
//
//===----------------------------------------------------------------------===//

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsdl_runtime.h"

static uint64_t parseHexU64(const char* s)
{
    return (uint64_t) strtoull(s, NULL, 16);
}

// Parse an even-length hex byte string into out; returns byte count (-1 on error).
static int parseHexBytes(const char* s, uint8_t* out, size_t out_cap)
{
    const size_t len = strlen(s);
    if ((len % 2U) != 0U || (len / 2U) > out_cap)
    {
        return -1;
    }
    for (size_t i = 0; i < len; i += 2U)
    {
        char        byte_hex[3] = {s[i], s[i + 1U], '\0'};
        char*       end         = NULL;
        const long  v           = strtol(byte_hex, &end, 16);
        if (end != byte_hex + 2)
        {
            return -1;
        }
        out[i / 2U] = (uint8_t) v;
    }
    return (int) (len / 2U);
}

static float bitsToF32(uint32_t bits)
{
    union
    {
        uint32_t u;
        float    f;
    } t;
    t.u = bits;
    return t.f;
}

static uint32_t f32ToBits(float value)
{
    union
    {
        float    f;
        uint32_t u;
    } t;
    t.f = value;
    return t.u;
}

static void fail(const char* op, const char* detail)
{
    fprintf(stderr, "MISMATCH %s: %s\n", op, detail);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <vectors-file>\n", argv[0]);
        return 2;
    }
    FILE* fp = fopen(argv[1], "r");
    if (fp == NULL)
    {
        fprintf(stderr, "cannot open vectors file: %s\n", argv[1]);
        return 2;
    }

    char   line[512];
    size_t processed = 0;
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char* hash = strchr(line, '#');
        if (hash != NULL)
        {
            *hash = '\0';
        }
        char        op[32];
        char        a[8][128];
        const int   n = sscanf(line, "%31s %127s %127s %127s %127s %127s %127s", op, a[0], a[1], a[2], a[3], a[4], a[5]);
        if (n <= 0)
        {
            continue;  // blank / comment-only line
        }

        if (strcmp(op, "f16pack") == 0 && n == 3)
        {
            const uint16_t got = dsdl_runtime_float16_pack(bitsToF32((uint32_t) parseHexU64(a[0])));
            const uint16_t want = (uint16_t) parseHexU64(a[1]);
            if (got != want)
            {
                char d[128];
                snprintf(d, sizeof(d), "in=%s want=%04x got=%04x", a[0], want, got);
                fail(op, d);
                return 1;
            }
        }
        else if (strcmp(op, "f16unpack") == 0 && n == 3)
        {
            const uint32_t got  = f32ToBits(dsdl_runtime_float16_unpack((uint16_t) parseHexU64(a[0])));
            const uint32_t want = (uint32_t) parseHexU64(a[1]);
            if (got != want)
            {
                char d[128];
                snprintf(d, sizeof(d), "in=%s want=%08x got=%08x", a[0], want, got);
                fail(op, d);
                return 1;
            }
        }
        else if ((strcmp(op, "geti") == 0 || strcmp(op, "getu") == 0) && n == 6)
        {
            const unsigned type_bits = (unsigned) strtoul(a[0], NULL, 10);
            const uint8_t  len_bits  = (uint8_t) strtoul(a[1], NULL, 10);
            const size_t   off_bits  = (size_t) strtoul(a[2], NULL, 10);
            uint8_t        buf[64];
            const int      blen = parseHexBytes(a[3], buf, sizeof(buf));
            const uint64_t want = parseHexU64(a[4]);
            if (blen < 0)
            {
                fail(op, "bad buffer hex");
                return 1;
            }
            uint64_t got = 0;
            if (strcmp(op, "geti") == 0)
            {
                int64_t s = 0;
                switch (type_bits)
                {
                case 8:  s = dsdl_runtime_get_i8((const uint8_t*) buf, (size_t) blen, off_bits, len_bits); break;
                case 16: s = dsdl_runtime_get_i16((const uint8_t*) buf, (size_t) blen, off_bits, len_bits); break;
                case 32: s = dsdl_runtime_get_i32((const uint8_t*) buf, (size_t) blen, off_bits, len_bits); break;
                case 64: s = dsdl_runtime_get_i64((const uint8_t*) buf, (size_t) blen, off_bits, len_bits); break;
                default: fail(op, "bad type_bits"); return 1;
                }
                got = (uint64_t) s;
            }
            else
            {
                switch (type_bits)
                {
                case 8:  got = dsdl_runtime_get_u8((const uint8_t*) buf, (size_t) blen, off_bits, len_bits); break;
                case 16: got = dsdl_runtime_get_u16((const uint8_t*) buf, (size_t) blen, off_bits, len_bits); break;
                case 32: got = dsdl_runtime_get_u32((const uint8_t*) buf, (size_t) blen, off_bits, len_bits); break;
                case 64: got = dsdl_runtime_get_u64((const uint8_t*) buf, (size_t) blen, off_bits, len_bits); break;
                default: fail(op, "bad type_bits"); return 1;
                }
            }
            if (got != want)
            {
                char d[160];
                snprintf(d, sizeof(d), "type=%u len=%u off=%zu buf=%s want=%016llx got=%016llx",
                         type_bits, len_bits, off_bits, a[3], (unsigned long long) want, (unsigned long long) got);
                fail(op, d);
                return 1;
            }
        }
        else if (strcmp(op, "copybits") == 0 && n == 7)
        {
            const size_t dst_off = (size_t) strtoul(a[0], NULL, 10);
            const size_t len_bits = (size_t) strtoul(a[1], NULL, 10);
            const size_t src_off = (size_t) strtoul(a[2], NULL, 10);
            uint8_t      src[64];
            uint8_t      dst[64];
            uint8_t      want[64];
            const int    slen = parseHexBytes(a[3], src, sizeof(src));
            const int    dlen = parseHexBytes(a[4], dst, sizeof(dst));
            const int    wlen = parseHexBytes(a[5], want, sizeof(want));
            if (slen < 0 || dlen < 0 || wlen < 0 || dlen != wlen)
            {
                fail(op, "bad buffer hex / length");
                return 1;
            }
            dsdl_runtime_copy_bits(dst, dst_off, len_bits, src, src_off);
            if (memcmp(dst, want, (size_t) dlen) != 0)
            {
                char d[256];
                int  k = snprintf(d, sizeof(d), "dstoff=%zu len=%zu srcoff=%zu got=", dst_off, len_bits, src_off);
                for (int i = 0; i < dlen && k < (int) sizeof(d) - 3; i++)
                {
                    k += snprintf(d + k, sizeof(d) - (size_t) k, "%02x", dst[i]);
                }
                fail(op, d);
                return 1;
            }
        }
        else
        {
            fprintf(stderr, "UNRECOGNIZED vector (op=%s n=%d): %s", op, n, line);
            return 1;
        }
        processed++;
    }
    fclose(fp);

    printf("PROCESSED %zu\n", processed);
    printf("C primitive equivalence PASS\n");
    return 0;
}
