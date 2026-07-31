/*
 * Part of the OpenCyphal project, under the MIT licence
 * SPDX-License-Identifier: MIT
 *
 * Shared by every C cell in the matrix. Nothing here is specific to a build system: if this compiles
 * and exits zero, the build found the generated headers, compiled the generated sources, linked
 * them, and could call them. That is the entire claim a build integration makes.
 *
 * It is not a serialiser test. Whether the bytes are right is settled by test/lit and
 * test/integration against the specification and the Dafny model; asserting it again here would only
 * mean a schema edit broke thirteen build cells at once. What this asserts is round-trip identity --
 * the weakest property that cannot pass by accident if the wiring is wrong.
 */

#include <stdio.h>
#include <string.h>

#include "kitbag/SensorFrame_1_0.h"

#define CHECK(cond, ...)                             \
    do {                                             \
        if (!(cond)) {                               \
            (void) fprintf(stderr, "FAIL: ");        \
            (void) fprintf(stderr, __VA_ARGS__);     \
            (void) fprintf(stderr, "\n");            \
            return 1;                                \
        }                                            \
    } while (0)

int main(void)
{
    /* Deliberately not the zero value: an integration that serialises nothing and deserialises
     * nothing would round-trip a zeroed struct perfectly and prove nothing at all. Every field is
     * set, both arrays are non-empty, and the nested composite carries a non-default mode. */
    kitbag__SensorFrame original;
    memset(&original, 0, sizeof(original));

    original.timestamp.microsecond = 1234567890123ULL;

    original.readings.count = 3U;
    for (size_t i = 0U; i < original.readings.count; ++i)
    {
        original.readings.elements[i].channel    = (uint8_t) (i + 1U);
        /* Exactly representable in binary32, so an equality comparison after the round trip is a
         * statement about the wiring rather than about floating-point rounding. */
        original.readings.elements[i].value      = 0.5F * (float) (i + 1U);
        original.readings.elements[i].mode.value = 2U; /* ACTIVE */
    }

    const char* const source = "imu.0";
    original.source.count    = strlen(source);
    memcpy(original.source.elements, source, original.source.count);

    uint8_t buffer[kitbag__SensorFrame_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t  size = sizeof(buffer);

    const int8_t ser = kitbag__SensorFrame__serialize_(&original, buffer, &size);
    CHECK(ser == DSDL_RUNTIME_SUCCESS, "serialize returned %d", (int) ser);

    kitbag__SensorFrame restored;
    memset(&restored, 0, sizeof(restored));

    size_t       consumed = size;
    const int8_t des      = kitbag__SensorFrame__deserialize_(&restored, buffer, &consumed);
    CHECK(des == DSDL_RUNTIME_SUCCESS, "deserialize returned %d", (int) des);

    CHECK(restored.timestamp.microsecond == original.timestamp.microsecond,
          "timestamp: %llu != %llu",
          (unsigned long long) restored.timestamp.microsecond,
          (unsigned long long) original.timestamp.microsecond);

    CHECK(restored.readings.count == original.readings.count,
          "readings.count: %zu != %zu", restored.readings.count, original.readings.count);

    for (size_t i = 0U; i < original.readings.count; ++i)
    {
        const kitbag__Reading* const a = &original.readings.elements[i];
        const kitbag__Reading* const b = &restored.readings.elements[i];
        CHECK(a->channel == b->channel, "readings[%zu].channel: %u != %u",
              i, (unsigned) b->channel, (unsigned) a->channel);
        CHECK(a->value == b->value, "readings[%zu].value: %f != %f",
              i, (double) b->value, (double) a->value);
        CHECK(a->mode.value == b->mode.value, "readings[%zu].mode: %u != %u",
              i, (unsigned) b->mode.value, (unsigned) a->mode.value);
    }

    CHECK(restored.source.count == original.source.count,
          "source.count: %zu != %zu", restored.source.count, original.source.count);
    CHECK(memcmp(restored.source.elements, original.source.elements, original.source.count) == 0,
          "source bytes differ");

    (void) printf("round-trip OK: %zu readings, %zu source bytes, %zu bytes on the wire\n",
                  restored.readings.count, restored.source.count, size);
    return 0;
}
