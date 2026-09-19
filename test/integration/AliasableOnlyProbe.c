//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Reads a known buffer through the accessors an accessors-only run emits: the outer type's
// composite getter answers the inner record's bytes, and the inner type's getter reads a field
// off them. Compiled against the generated output and nothing else.
//
//===----------------------------------------------------------------------===//

#include "fixtures_aliasable/vendor/Pose_1_0.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    // Six little-endian float32: position {1.5, -2.25, 3} then orientation {4, 5.5, 6.75}.
    const float f[6] = {1.5f, -2.25f, 3.0f, 4.0f, 5.5f, 6.75f};
    uint8_t     buffer[24];
    memcpy(buffer, f, sizeof buffer);

    size_t               orientation_size = 0;
    const uint8_t* const orientation =
        fixtures_aliasable__vendor__Pose__get_orientation_(buffer, sizeof buffer, &orientation_size);
    const float  y          = fixtures_aliasable__vendor__Vec3__get_y_(orientation, orientation_size);
    const int8_t set_result = fixtures_aliasable__vendor__Vec3__set_z_(buffer, 12, 9.5f);
    const float  z          = fixtures_aliasable__vendor__Vec3__get_z_(buffer, 12);
    const float  short_read = fixtures_aliasable__vendor__Vec3__get_z_(buffer, 4);
    const float  x =
        fixtures_aliasable__vendor__Vec3__get_x_(fixtures_aliasable__vendor__Pose__get_position_(buffer,
                                                                                                 sizeof buffer,
                                                                                                 NULL),
                                                 12);

    const int ok =
        orientation_size == 12 && y == 5.5f && set_result == 0 && z == 9.5f && short_read == 0.0f && x == 1.5f;
    printf("aliasable-only C: %s (orientation %zu bytes, y %g, set %d, z %g, short %g, x %g)\n",
           ok ? "ok" : "FAILED",
           orientation_size,
           (double) y,
           set_result,
           (double) z,
           (double) short_read,
           (double) x);
    return ok ? 0 : 1;
}
