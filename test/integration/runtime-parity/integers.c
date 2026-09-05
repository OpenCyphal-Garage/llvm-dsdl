// Holds the serialisation primitives built as IR against the ones the runtime header declares.
//
// `emit-dsdl-runtime` builds each primitive from the wire format rather than compiling the
// header, so nothing but a comparison keeps the two the same. Every call is made twice over the
// same randomised inputs and both the answer and the buffer are compared.
//
// Each pair is called in its own statement. Written as one expression the two calls are
// unsequenced, and the comparison then reads a buffer one of them has not finished with.

#include <stdio.h>
#include <string.h>
#include "dsdl_runtime.h"

void     ir_dsdl_runtime_copy_bits(void*, size_t, size_t, const void*, size_t);
void     ir_dsdl_runtime_get_bits(void*, const void*, size_t, size_t, size_t);
int8_t   ir_dsdl_runtime_set_uxx(uint8_t*, size_t, size_t, uint64_t, uint8_t);
int8_t   ir_dsdl_runtime_set_ixx(uint8_t*, size_t, size_t, int64_t, uint8_t);
int8_t   ir_dsdl_runtime_set_bit(uint8_t*, size_t, size_t, bool);
uint8_t  ir_dsdl_runtime_get_u8(const uint8_t*, size_t, size_t, uint8_t);
uint16_t ir_dsdl_runtime_get_u16(const uint8_t*, size_t, size_t, uint8_t);
uint32_t ir_dsdl_runtime_get_u32(const uint8_t*, size_t, size_t, uint8_t);
uint64_t ir_dsdl_runtime_get_u64(const uint8_t*, size_t, size_t, uint8_t);
int8_t   ir_dsdl_runtime_get_i8(const uint8_t*, size_t, size_t, uint8_t);
int16_t  ir_dsdl_runtime_get_i16(const uint8_t*, size_t, size_t, uint8_t);
int32_t  ir_dsdl_runtime_get_i32(const uint8_t*, size_t, size_t, uint8_t);
int64_t  ir_dsdl_runtime_get_i64(const uint8_t*, size_t, size_t, uint8_t);
bool     ir_dsdl_runtime_get_bit(const uint8_t*, size_t, size_t);

static unsigned long long s = 88172645463325252ull;
static unsigned           rnd(unsigned n)
{
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return (unsigned) (s % n);
}

#define FAIL(what, ...)                          \
    do                                           \
    {                                            \
        if (++failures <= 3)                     \
            printf("  " what "\n", __VA_ARGS__); \
    } while (0)

int main(void)
{
    int failures = 0;
    for (int t = 0; t < 40000; ++t)
    {
        uint8_t buf[24], a[24], b[24];
        for (size_t i = 0; i < sizeof buf; ++i)
        {
            buf[i] = (uint8_t) rnd(256);
            a[i] = b[i] = (uint8_t) rnd(256);
        }
        const size_t   size = 1 + rnd(sizeof buf);
        const size_t   off  = rnd(size * 8 + 16);
        const uint8_t  len  = (uint8_t) rnd(70);
        const uint64_t val  = ((uint64_t) rnd(0xFFFFFFFFu) << 32) | rnd(0xFFFFFFFFu);

        {
            const int8_t rc_c = dsdl_runtime_set_uxx(a, size, off, val, len);
            const int8_t rc_i = ir_dsdl_runtime_set_uxx(b, size, off, val, len);
            if (rc_c != rc_i || memcmp(a, b, sizeof a))
            {
                if (++failures <= 2)
                {
                    printf("set_uxx: size=%zu off=%zu len=%u val=%llx rc %d/%d\n",
                           size,
                           off,
                           len,
                           (unsigned long long) val,
                           rc_c,
                           rc_i);
                    printf("   C : ");
                    for (int k = 0; k < (int) sizeof a; ++k)
                        printf("%02X", a[k]);
                    printf("\n");
                    printf("   IR: ");
                    for (int k = 0; k < (int) sizeof b; ++k)
                        printf("%02X", b[k]);
                    printf("\n");
                }
            }
        }
        memcpy(b, a, sizeof a);
        {
            const int8_t rc_c = dsdl_runtime_set_ixx(a, size, off, (int64_t) val, len);
            const int8_t rc_i = ir_dsdl_runtime_set_ixx(b, size, off, (int64_t) val, len);
            if (rc_c != rc_i || memcmp(a, b, sizeof a))
            {
                if (++failures <= 2)
                {
                    printf("set_ixx: size=%zu off=%zu len=%u rc %d/%d  buffers %s\n",
                           size,
                           off,
                           len,
                           rc_c,
                           rc_i,
                           memcmp(a, b, sizeof a) ? "DIFFER" : "equal");
                }
            }
        }
        memcpy(b, a, sizeof a);
        const bool bit = (val & 1u) != 0;
        {
            uint8_t before[24];
            memcpy(before, a, sizeof before);
            const int8_t rc_c = dsdl_runtime_set_bit(a, size, off, bit);
            const int8_t rc_i = ir_dsdl_runtime_set_bit(b, size, off, bit);
            if (rc_c != rc_i || memcmp(a, b, sizeof a))
            {
                if (++failures <= 2)
                {
                    printf("set_bit: size=%zu off=%zu bit=%d rc %d/%d\n", size, off, (int) bit, rc_c, rc_i);
                    printf("   in: ");
                    for (int k = 0; k < 8; ++k)
                        printf("%02X", before[k]);
                    printf("\n");
                    printf("   C : ");
                    for (int k = 0; k < 8; ++k)
                        printf("%02X", a[k]);
                    printf("\n");
                    printf("   IR: ");
                    for (int k = 0; k < 8; ++k)
                        printf("%02X", b[k]);
                    printf("\n");
                }
            }
        }

        if (dsdl_runtime_get_u8(buf, size, off, len) != ir_dsdl_runtime_get_u8(buf, size, off, len))
            FAIL("get_u8  differs: size=%zu off=%zu len=%u", size, off, len);
        if (dsdl_runtime_get_u16(buf, size, off, len) != ir_dsdl_runtime_get_u16(buf, size, off, len))
            FAIL("get_u16 differs: size=%zu off=%zu len=%u", size, off, len);
        if (dsdl_runtime_get_u32(buf, size, off, len) != ir_dsdl_runtime_get_u32(buf, size, off, len))
            FAIL("get_u32 differs: size=%zu off=%zu len=%u", size, off, len);
        if (dsdl_runtime_get_u64(buf, size, off, len) != ir_dsdl_runtime_get_u64(buf, size, off, len))
            FAIL("get_u64 differs: size=%zu off=%zu len=%u", size, off, len);
        if (dsdl_runtime_get_i8(buf, size, off, len) != ir_dsdl_runtime_get_i8(buf, size, off, len))
            FAIL("get_i8  differs: size=%zu off=%zu len=%u", size, off, len);
        if (dsdl_runtime_get_i16(buf, size, off, len) != ir_dsdl_runtime_get_i16(buf, size, off, len))
            FAIL("get_i16 differs: size=%zu off=%zu len=%u", size, off, len);
        if (dsdl_runtime_get_i32(buf, size, off, len) != ir_dsdl_runtime_get_i32(buf, size, off, len))
            FAIL("get_i32 differs: size=%zu off=%zu len=%u", size, off, len);
        if (dsdl_runtime_get_i64(buf, size, off, len) != ir_dsdl_runtime_get_i64(buf, size, off, len))
            FAIL("get_i64 differs: size=%zu off=%zu len=%u", size, off, len);
        if (dsdl_runtime_get_bit(buf, size, off) != ir_dsdl_runtime_get_bit(buf, size, off))
            FAIL("get_bit differs: size=%zu off=%zu", size, off);

        uint8_t o1[16], o2[16];
        memset(o1, 0xAA, sizeof o1);
        memset(o2, 0xAA, sizeof o2);
        const size_t glen = rnd(96);
        dsdl_runtime_get_bits(o1, buf, size, off, glen);
        ir_dsdl_runtime_get_bits(o2, buf, size, off, glen);
        if (memcmp(o1, o2, (glen + 7) / 8))
            FAIL("get_bits differs: size=%zu off=%zu len=%zu", size, off, glen);
    }
    if (failures)
    {
        printf("%d differences\n", failures);
        return 1;
    }
    printf("integer primitives: 40000 trials x 14 primitives agree\n");
    return 0;
}
