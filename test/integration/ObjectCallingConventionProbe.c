//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Each entry point of an object dsdlc emitted, called through its header and asked for an error,
// and each kind of value a field accessor carries, written and read back.
//
// The header declares the entry points' `int8_t` answers and the accessors' member types, and a
// caller compiled from it passes and takes each narrow value as the C ABI extends it. An object that
// answers a code in the low byte alone reads as its unsigned value, so each answer here is compared
// as an `int` or a 64-bit integer with the value it stands for.
//
//===----------------------------------------------------------------------===//

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "dsdl_runtime.h"
#include "fixtures_accessor_types/vendor/Kinds_1_0.h"
#include "fixtures_aliasable/vendor/Vec3_1_0.h"

static int check(const char* const what, const int64_t answered, const int64_t expected)
{
    if (answered != expected)
    {
        printf("FAIL %s: answered %lld, expected %lld\n", what, (long long) answered, (long long) expected);
        return 1;
    }
    return 0;
}

int main(void)
{
    const int invalid  = -DSDL_RUNTIME_ERROR_INVALID_ARGUMENT;
    const int tooSmall = -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;

    struct fixtures_aliasable__vendor__Vec3 vector;
    uint8_t                                 buffer[fixtures_aliasable__vendor__Vec3_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t                                  size     = sizeof buffer;
    int                                     failures = 0;

    failures += check("initialize_ of a null object", fixtures_aliasable__vendor__Vec3__initialize_(NULL), invalid);
    failures += check("initialize_", fixtures_aliasable__vendor__Vec3__initialize_(&vector), 0);

    failures += check("serialize_ of a null object",
                      fixtures_aliasable__vendor__Vec3__serialize_(NULL, buffer, &size),
                      invalid);
    size = 1U;
    failures += check("serialize_ into a short buffer",
                      fixtures_aliasable__vendor__Vec3__serialize_(&vector, buffer, &size),
                      tooSmall);
    size = sizeof buffer;
    failures += check("serialize_", fixtures_aliasable__vendor__Vec3__serialize_(&vector, buffer, &size), 0);

    failures += check("deserialize_ into a null object",
                      fixtures_aliasable__vendor__Vec3__deserialize_(NULL, buffer, &size),
                      invalid);

    failures += check("set_x_ into a null buffer", fixtures_aliasable__vendor__Vec3__set_x_(NULL, 0U, 1.0F), invalid);
    failures +=
        check("set_x_ into a short buffer", fixtures_aliasable__vendor__Vec3__set_x_(buffer, 1U, 1.0F), tooSmall);
    failures += check("set_x_", fixtures_aliasable__vendor__Vec3__set_x_(buffer, sizeof buffer, 1.0F), 0);

    uint8_t kinds[fixtures_accessor_types__vendor__Kinds_SERIALIZATION_BUFFER_SIZE_BYTES_] = {0};
    failures += check("set_small_", fixtures_accessor_types__vendor__Kinds__set_small_(kinds, sizeof kinds, -1), 0);
    failures += check("get_small_", fixtures_accessor_types__vendor__Kinds__get_small_(kinds, sizeof kinds), -1);
    failures +=
        check("set_wide_", fixtures_accessor_types__vendor__Kinds__set_wide_(kinds, sizeof kinds, UINT16_MAX), 0);
    failures += check("get_wide_", fixtures_accessor_types__vendor__Kinds__get_wide_(kinds, sizeof kinds), UINT16_MAX);
    failures +=
        check("set_large_", fixtures_accessor_types__vendor__Kinds__set_large_(kinds, sizeof kinds, INT64_MIN), 0);
    failures += check("get_large_", fixtures_accessor_types__vendor__Kinds__get_large_(kinds, sizeof kinds), INT64_MIN);
    failures +=
        check("set_flags_", fixtures_accessor_types__vendor__Kinds__set_flags_(kinds, sizeof kinds, 3U, true), 0);
    failures += check("get_flags_ of the element set",
                      fixtures_accessor_types__vendor__Kinds__get_flags_(kinds, sizeof kinds, 3U),
                      1);
    failures += check("get_flags_ of an element left clear",
                      fixtures_accessor_types__vendor__Kinds__get_flags_(kinds, sizeof kinds, 2U),
                      0);

    if (failures != 0)
    {
        return 1;
    }
    printf("ok\n");
    return 0;
}
