//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading of a container holding a view, through the C++ member functions, for both profiles.
//
//===----------------------------------------------------------------------===//

#include "fixtures_aliasable/vendor/Pose_1_0.hpp"
#include "fixtures_aliasable/vendor/Vec3_1_0.hpp"
#include "fixtures_views/vendor/Frame_1_0.hpp"
#include <cstdio>
#include <cstring>
static int  failures = 0;
static void check(const char* what, bool ok)
{
    std::printf("  %-44s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}
int main()
{
    using fixtures_views::vendor::Frame;
    using fixtures_aliasable::vendor::Pose;
    using fixtures_aliasable::vendor::Vec3;
    std::uint8_t wire[41];
    std::memset(wire, 0, sizeof wire);
    const std::uint32_t seq = 0x11223344U;
    std::memcpy(wire, &seq, 4);
    const float pose[6] = {1.5f, -2.25f, 3.0f, 4.0f, 5.5f, 6.75f};
    std::memcpy(wire + 4, pose, 24);
    const float vel[3] = {7.0f, 8.0f, 9.0f};
    std::memcpy(wire + 28, vel, 12);
    wire[40] = 0x5A;
    Frame       frame;
    std::size_t size = sizeof wire;
    check("deserialise accepted", frame.deserialize(wire, &size) == 0 && size == sizeof wire);
    check("sequence decoded", frame.sequence == seq);
    check("view points into the buffer", frame.pose.bytes == wire + 4 && frame.pose.size_bytes == 24);
    std::size_t         n = 0;
    const std::uint8_t* o = Pose::get_orientation(frame.pose.bytes, frame.pose.size_bytes, &n);
    check("orientation.y read through the view", Vec3::get_y(o, n) == 5.5f);
    check("velocity decoded, status after the view", frame.velocity.y == 8.0f && frame.status == 0x5A);
    std::uint8_t out[64];
    std::memset(out, 0xEE, sizeof out);
    std::size_t out_size = sizeof out;
    check("serialise reproduces the wire",
          frame.serialize(out, &out_size) == 0 && out_size == 41 && std::memcmp(out, wire, 41) == 0);
    Frame       short_frame;
    std::size_t short_size = 16;
    check("short deserialise accepted", short_frame.deserialize(wire, &short_size) == 0);
    check("short view holds what was there", short_frame.pose.bytes == wire + 4 && short_frame.pose.size_bytes == 12);
    o = Pose::get_orientation(short_frame.pose.bytes, short_frame.pose.size_bytes, &n);
    check("missing orientation reads as zero", n == 0 && Vec3::get_y(o, n) == 0.0f && short_frame.status == 0);
    std::memset(out, 0xEE, sizeof out);
    out_size = sizeof out;
    check("short view serialises zero-filled",
          short_frame.serialize(out, &out_size) == 0 && std::memcmp(out + 4, wire + 4, 12) == 0 && out[16] == 0 &&
              out[27] == 0);
    Frame fresh;
    check("fresh object holds an empty view", fresh.pose.bytes == nullptr && fresh.pose.size_bytes == 0);
    std::memset(out, 0xEE, sizeof out);
    out_size   = sizeof out;
    bool zeros = fresh.serialize(out, &out_size) == 0;
    for (int i = 4; i < 28; ++i)
        zeros = zeros && out[i] == 0;
    check("empty view serialises as zeros", zeros);
    std::printf("container-views C++: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures == 0 ? 0 : 1;
}
