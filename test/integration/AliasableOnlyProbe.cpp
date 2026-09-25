//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading, through the C++ static-member accessors, for both profiles.
//
//===----------------------------------------------------------------------===//

#include "fixtures_aliasable/vendor/Pose_1_0.hpp"
#include "fixtures_aliasable/vendor/Vec3_1_0.hpp"

#include <cstdio>
#include <cstring>
#include <span>

int main()
{
    const float  f[6] = {1.5f, -2.25f, 3.0f, 4.0f, 5.5f, 6.75f};
    std::uint8_t buffer[24];
    std::memcpy(buffer, f, sizeof buffer);

    using fixtures_aliasable::vendor::Pose;
    using fixtures_aliasable::vendor::Vec3;

    const std::span<const std::uint8_t> orientation = Pose::get_orientation(buffer);
    const float                         y           = Vec3::get_y(orientation);
    const std::int8_t                   set_result  = Vec3::set_z(std::span<std::uint8_t>(buffer, 12), 9.5f);
    const float                         z           = Vec3::get_z(std::span<const std::uint8_t>(buffer, 12));
    const float                         short_read  = Vec3::get_z(std::span<const std::uint8_t>(buffer, 4));
    const float                         x           = Vec3::get_x(Pose::get_position(buffer));

    const bool ok =
        orientation.size() == 12 && y == 5.5f && set_result == 0 && z == 9.5f && short_read == 0.0f && x == 1.5f;
    std::printf("aliasable-only C++: %s (orientation %zu bytes, y %g, set %d, z %g, short %g, x %g)\n",
                ok ? "ok" : "FAILED",
                orientation.size(),
                static_cast<double>(y),
                static_cast<int>(set_result),
                static_cast<double>(z),
                static_cast<double>(short_read),
                static_cast<double>(x));
    return ok ? 0 : 1;
}
