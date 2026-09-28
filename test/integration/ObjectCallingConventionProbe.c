//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Each entry point of an object dsdlc emitted, called through its header and asked for an error.
//
// The header declares the entry points' `int8_t` answers, and a caller compiled from it takes each
// answer as the C ABI extends it. An object that answers the code in the low byte alone reads as
// its unsigned value, so each refusal here is compared as an `int` with the runtime's negated code.
//
//===----------------------------------------------------------------------===//

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "dsdl_runtime.h"
#include "fixtures_aliasable/vendor/Vec3_1_0.h"

static int check(const char* const what, const int answered, const int expected)
{
    if (answered != expected)
    {
        printf("FAIL %s: answered %d, expected %d\n", what, answered, expected);
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

    if (failures != 0)
    {
        return 1;
    }
    printf("ok\n");
    return 0;
}
