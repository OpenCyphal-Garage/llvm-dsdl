#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "uavcan/node/Version_1_0.h"
#include "uavcan/primitive/scalar/Integer16_1_0.h"
#include "uavcan/primitive/scalar/Natural64_1_0.h"
#include "uavcan/primitive/scalar/Real16_1_0.h"
#include "uavcan/primitive/scalar/Real64_1_0.h"
#include "uavcan/si/unit/temperature/Scalar_1_0.h"
#include "uavcan/file/Error_1_0.h"

/* An accessor against the body it stands in for: a getter answers what deserialise puts in the
 * field, on a full buffer and on a short one, and a setter writes what deserialise reads back. The
 * buffers are pseudo-random, so floats meet every pattern including the NaNs, and values are
 * compared as bits. An integer round trip is also exact; a float's is not asked to be, since a
 * float16 NaN need not survive narrowing with its payload. */
static uint32_t rng = 0x9E3779B9u;

static void fill(uint8_t* const buffer, const size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        buffer[i] = (uint8_t) rng;
    }
}

static int failures = 0;

#define CHECK_FIELD(T, SIZE, FIELD, CTYPE, EXACT)                           \
    do                                                                      \
    {                                                                       \
        uint8_t wire[SIZE];                                                 \
        uint8_t out[SIZE];                                                  \
        T       obj;                                                        \
        CTYPE   got;                                                        \
        CTYPE   v;                                                          \
        size_t  size = SIZE;                                                \
        int     ok   = 1;                                                   \
        fill(wire, SIZE);                                                   \
        ok   = ok && (T##__deserialize_(&obj, wire, &size) == 0);           \
        got  = T##__get_##FIELD##_(wire, SIZE);                             \
        ok   = ok && (memcmp(&got, &obj.FIELD, sizeof got) == 0);           \
        size = SIZE / 2;                                                    \
        ok   = ok && (T##__deserialize_(&obj, wire, &size) == 0);           \
        got  = T##__get_##FIELD##_(wire, SIZE / 2);                         \
        ok   = ok && (memcmp(&got, &obj.FIELD, sizeof got) == 0);           \
        memset(out, 0, SIZE);                                               \
        v    = T##__get_##FIELD##_(wire, SIZE);                             \
        ok   = ok && (T##__set_##FIELD##_(out, SIZE, v) == 0);              \
        size = SIZE;                                                        \
        ok   = ok && (T##__deserialize_(&obj, out, &size) == 0);            \
        got  = T##__get_##FIELD##_(out, SIZE);                              \
        ok   = ok && (memcmp(&got, &obj.FIELD, sizeof got) == 0);           \
        ok   = ok && (!(EXACT) || (memcmp(&v, &obj.FIELD, sizeof v) == 0)); \
        ok   = ok && (T##__set_##FIELD##_(out, 0, v) != 0);                 \
        if (!ok)                                                            \
        {                                                                   \
            ++type_failures;                                                \
        }                                                                   \
    } while (0)

#define REPORT(NAME)                                                        \
    do                                                                      \
    {                                                                       \
        printf("%-40s %s\n", NAME, type_failures == 0 ? "same" : "DIFFER"); \
        failures += type_failures;                                          \
        type_failures = 0;                                                  \
    } while (0)

int main(void)
{
    int type_failures = 0;
    CHECK_FIELD(uavcan__node__Version, 2, major, uint8_t, 1);
    CHECK_FIELD(uavcan__node__Version, 2, minor, uint8_t, 1);
    REPORT("uavcan.node.Version");
    CHECK_FIELD(uavcan__primitive__scalar__Integer16, 2, value, int16_t, 1);
    REPORT("uavcan.primitive.scalar.Integer16");
    CHECK_FIELD(uavcan__primitive__scalar__Natural64, 8, value, uint64_t, 1);
    REPORT("uavcan.primitive.scalar.Natural64");
    CHECK_FIELD(uavcan__primitive__scalar__Real16, 2, value, float, 0);
    REPORT("uavcan.primitive.scalar.Real16");
    CHECK_FIELD(uavcan__primitive__scalar__Real64, 8, value, double, 0);
    REPORT("uavcan.primitive.scalar.Real64");
    CHECK_FIELD(uavcan__si__unit__temperature__Scalar, 4, kelvin, float, 0);
    REPORT("uavcan.si.unit.temperature.Scalar");
    CHECK_FIELD(uavcan__file__Error, 2, value, uint16_t, 1);
    REPORT("uavcan.file.Error");
    return failures == 0 ? 0 : 1;
}
