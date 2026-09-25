//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading of an equal-length union, through the C++ static-member accessors.
//
//===----------------------------------------------------------------------===//

#include "fixtures_union/vendor/Choice_1_0.hpp"
#include <cstdio>
#include <cstring>
#include <span>
int main()
{
    std::uint8_t wire[5] = {1, 0, 0, 0, 0};
    const float  real    = 5.5f;
    std::memcpy(wire + 1, &real, 4);
    using fixtures_union::vendor::Choice;
    const std::span<const std::uint8_t> quad = Choice::get_quad(wire);
    const bool ok = Choice::get_tag_(wire) == 1 && Choice::get_real(wire) == 5.5f && quad.data() == &wire[1] &&
                    quad.size() == 4 && Choice::set_tag_(wire, 0) == 0;
    std::printf("union-accessors C++: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
