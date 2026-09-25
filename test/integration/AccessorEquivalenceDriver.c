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
#include "uavcan/si/unit/angle/Quaternion_1_0.h"
#include "uavcan/si/sample/temperature/Scalar_1_0.h"

/* An accessor against the body it stands in for: a getter answers what deserialise puts in the
 * field, on a full buffer and on a short one, and a setter writes what deserialise reads back. The
 * buffers are pseudo-random, so floats meet every pattern including the NaNs, and values are
 * compared as bits. An integer round trip is also exact; a float's is not asked to be, since a
 * float16 NaN need not survive narrowing with its payload. A getter handed a null buffer reads it
 * as an empty one, whatever size it is handed, and answers zero. */
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
        CTYPE   zero;                                                       \
        size_t  size = SIZE;                                                \
        int     ok   = 1;                                                   \
        fill(wire, SIZE);                                                   \
        memset(&zero, 0, sizeof zero);                                      \
        ok   = ok && (T##__deserialize_(&obj, wire, &size) == 0);           \
        got  = T##__get_##FIELD##_(wire, SIZE);                             \
        ok   = ok && (memcmp(&got, &obj.FIELD, sizeof got) == 0);           \
        got  = T##__get_##FIELD##_(NULL, SIZE);                             \
        ok   = ok && (memcmp(&got, &zero, sizeof got) == 0);                \
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

/* An element of a fixed array, through the index the accessor takes; one past the capacity reads as
 * zero and cannot be set. */
#define CHECK_ELEMENT(T, SIZE, FIELD, INDEX, CAPACITY, CTYPE)            \
    do                                                                   \
    {                                                                    \
        uint8_t wire[SIZE];                                              \
        uint8_t out[SIZE];                                               \
        T       obj;                                                     \
        CTYPE   got;                                                     \
        CTYPE   v;                                                       \
        CTYPE   zero;                                                    \
        size_t  size = SIZE;                                             \
        int     ok   = 1;                                                \
        fill(wire, SIZE);                                                \
        memset(&zero, 0, sizeof zero);                                   \
        ok  = ok && (T##__deserialize_(&obj, wire, &size) == 0);         \
        got = T##__get_##FIELD##_(wire, SIZE, INDEX);                    \
        ok  = ok && (memcmp(&got, &obj.FIELD[INDEX], sizeof got) == 0);  \
        got = T##__get_##FIELD##_(wire, SIZE, CAPACITY);                 \
        ok  = ok && (memcmp(&got, &zero, sizeof got) == 0);              \
        got = T##__get_##FIELD##_(NULL, SIZE, INDEX);                    \
        ok  = ok && (memcmp(&got, &zero, sizeof got) == 0);              \
        memset(out, 0, SIZE);                                            \
        v    = T##__get_##FIELD##_(wire, SIZE, INDEX);                   \
        ok   = ok && (T##__set_##FIELD##_(out, SIZE, INDEX, v) == 0);    \
        size = SIZE;                                                     \
        ok   = ok && (T##__deserialize_(&obj, out, &size) == 0);         \
        got  = T##__get_##FIELD##_(out, SIZE, INDEX);                    \
        ok   = ok && (memcmp(&got, &obj.FIELD[INDEX], sizeof got) == 0); \
        ok   = ok && (T##__set_##FIELD##_(out, SIZE, CAPACITY, v) != 0); \
        if (!ok)                                                         \
        {                                                                \
            ++type_failures;                                             \
        }                                                                \
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
    for (size_t i = 0; i < 4; ++i)
    {
        CHECK_ELEMENT(uavcan__si__unit__angle__Quaternion, 16, wxyz, i, 4, float);
    }
    REPORT("uavcan.si.unit.angle.Quaternion");
    /* A nested composite, through the buffer its getter answers: the nested type's own getter on it
     * agrees with deserialise on the full buffer and on one cut inside the nested field. The field is
     * at offset nought, so the getter answers the buffer it was handed, a null one with a size of
     * zero, which the nested getter reads as empty. A caller that needs no size passes a null pointer
     * for it. */
    {
        uint8_t                                 wire[11];
        uavcan__si__sample__temperature__Scalar obj;
        size_t                                  size = sizeof wire;
        size_t                                  sub  = 0;
        int                                     ok   = 1;
        fill(wire, sizeof wire);
        ok                   = ok && (uavcan__si__sample__temperature__Scalar__deserialize_(&obj, wire, &size) == 0);
        const uint8_t* stamp = uavcan__si__sample__temperature__Scalar__get_timestamp_(wire, sizeof wire, &sub);
        ok    = ok && (uavcan__time__SynchronizedTimestamp__get_microsecond_(stamp, sub) == obj.timestamp.microsecond);
        ok    = ok && (uavcan__si__sample__temperature__Scalar__get_timestamp_(wire, sizeof wire, NULL) == stamp);
        size  = 3;
        ok    = ok && (uavcan__si__sample__temperature__Scalar__deserialize_(&obj, wire, &size) == 0);
        stamp = uavcan__si__sample__temperature__Scalar__get_timestamp_(wire, 3, &sub);
        ok    = ok && (sub == 3) &&
                (uavcan__time__SynchronizedTimestamp__get_microsecond_(stamp, sub) == obj.timestamp.microsecond);
        stamp = uavcan__si__sample__temperature__Scalar__get_timestamp_(NULL, sizeof wire, &sub);
        ok    = ok && (stamp == NULL) && (sub == 0) &&
                (uavcan__time__SynchronizedTimestamp__get_microsecond_(stamp, sub) == 0);
        if (!ok)
        {
            ++type_failures;
        }
        CHECK_FIELD(uavcan__si__sample__temperature__Scalar, 11, kelvin, float, 0);
    }
    REPORT("uavcan.si.sample.temperature.Scalar");
    return failures == 0 ? 0 : 1;
}
