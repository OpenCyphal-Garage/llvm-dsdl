/*
 * Part of the OpenCyphal project, under the MIT licence
 * SPDX-License-Identifier: MIT
 *
 * Shared by every C recipe in the matrix. Nothing here is specific to a build system: if this
 * compiles and exits zero, the build found the generated headers, compiled the generated sources,
 * linked them, and could call them. That is the entire claim a build integration makes.
 *
 * It is not a serialiser test. Whether the bytes are right is settled by test/lit and
 * test/integration against the specification and the Dafny model; asserting it again here would only
 * mean a schema edit broke thirteen recipes at once. What this asserts is round-trip identity -- the
 * weakest property that cannot pass by accident if the wiring is wrong.
 *
 * `lanyard.health.SystemHealth.1.0` is the type under test because of what it drags in: two standard
 * types from the compiled-in catalog, a bounded array of a local composite, and a delimited extent.
 * An integration that compiles the top-level type but not what it embeds fails here rather than in a
 * user's build.
 */

#include <stdio.h>
#include <string.h>

#include "lanyard/health/SystemHealth_1_0.h"

#define CHECK(cond, ...)                             \
    do {                                             \
        if (!(cond)) {                               \
            (void) fprintf(stderr, "FAIL: ");        \
            (void) fprintf(stderr, __VA_ARGS__);     \
            (void) fprintf(stderr, "\n");            \
            return 1;                                \
        }                                            \
    } while (0)

static const char* const kSubsystemNames[] = {"gnss", "esc.3", "imu.0"};

int main(void)
{
    /* Deliberately not the zero value: an integration that serialised nothing and deserialised
     * nothing would round-trip a zeroed struct perfectly and prove nothing at all. Every field is
     * set, the array is non-empty, and each element carries a non-default nested value. */
    lanyard__health__SystemHealth original;
    memset(&original, 0, sizeof(original));

    original.timestamp.microsecond  = 1234567890123ULL;
    original.aggregate_health.value = 2U; /* CAUTION, on the standard four-level scale */
    original.subsystem.count        = sizeof(kSubsystemNames) / sizeof(kSubsystemNames[0]);

    for (size_t i = 0U; i < original.subsystem.count; ++i)
    {
        lanyard__health__SubsystemReport* const report = &original.subsystem.elements[i];
        report->health.value   = (uint8_t) (i % 4U);
        report->severity.value = (uint8_t) (i % 8U);
        report->fault_code     = (uint16_t) (0x1000U + i);
        report->name.count     = strlen(kSubsystemNames[i]);
        memcpy(report->name.elements, kSubsystemNames[i], report->name.count);
    }

    uint8_t buffer[lanyard__health__SystemHealth_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t  size = sizeof(buffer);

    const int8_t ser = lanyard__health__SystemHealth__serialize_(&original, buffer, &size);
    CHECK(ser == DSDL_RUNTIME_SUCCESS, "serialize returned %d", (int) ser);

    lanyard__health__SystemHealth restored;
    memset(&restored, 0, sizeof(restored));

    size_t       consumed = size;
    const int8_t des = lanyard__health__SystemHealth__deserialize_(&restored, buffer, &consumed);
    CHECK(des == DSDL_RUNTIME_SUCCESS, "deserialize returned %d", (int) des);

    CHECK(restored.timestamp.microsecond == original.timestamp.microsecond,
          "timestamp: %llu != %llu",
          (unsigned long long) restored.timestamp.microsecond,
          (unsigned long long) original.timestamp.microsecond);
    CHECK(restored.aggregate_health.value == original.aggregate_health.value,
          "aggregate_health: %u != %u",
          (unsigned) restored.aggregate_health.value,
          (unsigned) original.aggregate_health.value);
    CHECK(restored.subsystem.count == original.subsystem.count,
          "subsystem.count: %zu != %zu", restored.subsystem.count, original.subsystem.count);

    for (size_t i = 0U; i < original.subsystem.count; ++i)
    {
        const lanyard__health__SubsystemReport* const a = &original.subsystem.elements[i];
        const lanyard__health__SubsystemReport* const b = &restored.subsystem.elements[i];
        CHECK(a->health.value == b->health.value, "subsystem[%zu].health: %u != %u",
              i, (unsigned) b->health.value, (unsigned) a->health.value);
        CHECK(a->severity.value == b->severity.value, "subsystem[%zu].severity: %u != %u",
              i, (unsigned) b->severity.value, (unsigned) a->severity.value);
        CHECK(a->fault_code == b->fault_code, "subsystem[%zu].fault_code: %u != %u",
              i, (unsigned) b->fault_code, (unsigned) a->fault_code);
        CHECK(a->name.count == b->name.count, "subsystem[%zu].name.count: %zu != %zu",
              i, b->name.count, a->name.count);
        CHECK(memcmp(a->name.elements, b->name.elements, a->name.count) == 0,
              "subsystem[%zu].name bytes differ", i);
    }

    (void) printf("round-trip OK: %zu subsystems, %zu bytes on the wire\n",
                  restored.subsystem.count, size);
    return 0;
}
