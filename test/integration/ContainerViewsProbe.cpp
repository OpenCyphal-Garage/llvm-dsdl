//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading of a container holding a view, through the C++ member functions, for each
// profile. A view is the profile's span, which the record's accessors take as it is held.
//
//===----------------------------------------------------------------------===//

#include "fixtures_aliasable/vendor/Pose_1_0.hpp"
#include "fixtures_aliasable/vendor/Vec3_1_0.hpp"
#include "fixtures_views/vendor/Frame_1_0.hpp"
#include "fixtures_views/vendor/Leading_1_0.hpp"
#include "fixtures_views/vendor/Track_1_0.hpp"
#include <cstdio>
#include <cstring>

// The span a view is held in, as the lane's vocabulary binds it for the profile under test.
#ifndef LLVMDSDL_SPAN_HEADER
#    define LLVMDSDL_SPAN_HEADER <span>
#    define LLVMDSDL_SPAN std::span
#endif
#include LLVMDSDL_SPAN_HEADER
using byte_span = LLVMDSDL_SPAN<const std::uint8_t>;

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
    check("view points into the buffer", frame.pose.data() == wire + 4 && frame.pose.size() == 24);
    byte_span o = Pose::get_orientation(frame.pose);
    check("orientation.y read through the view", Vec3::get_y(o) == 5.5f);
    check("velocity decoded, status after the view", frame.velocity.y == 8.0f && frame.status == 0x5A);
    std::uint8_t out[64];
    std::memset(out, 0xEE, sizeof out);
    std::size_t out_size = sizeof out;
    check("serialise reproduces the wire",
          frame.serialize(out, &out_size) == 0 && out_size == 41 && std::memcmp(out, wire, 41) == 0);
    Frame       short_frame;
    std::size_t short_size = 16;
    check("short deserialise accepted", short_frame.deserialize(wire, &short_size) == 0);
    check("short view holds what was there", short_frame.pose.data() == wire + 4 && short_frame.pose.size() == 12);
    o = Pose::get_orientation(short_frame.pose);
    check("missing orientation reads as zero", o.empty() && Vec3::get_y(o) == 0.0f && short_frame.status == 0);
    std::memset(out, 0xEE, sizeof out);
    out_size = sizeof out;
    check("short view serialises zero-filled",
          short_frame.serialize(out, &out_size) == 0 && std::memcmp(out + 4, wire + 4, 12) == 0 && out[16] == 0 &&
              out[27] == 0);
    Frame fresh;
    check("fresh object holds an empty view", fresh.pose.data() == nullptr && fresh.pose.empty());
    std::memset(out, 0xEE, sizeof out);
    out_size   = sizeof out;
    bool zeros = fresh.serialize(out, &out_size) == 0;
    for (int i = 4; i < 28; ++i)
        zeros = zeros && out[i] == 0;
    check("empty view serialises as zeros", zeros);
    using fixtures_views::vendor::Track;
    std::uint8_t track_wire[99];
    std::memset(track_wire, 0, sizeof track_wire);
    track_wire[0]           = 0x07;
    const float poses[4][6] = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
                               {10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f},
                               {-1.0f, -2.0f, -3.0f, -4.0f, -5.0f, -6.0f},
                               {0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f}};
    std::memcpy(track_wire + 1, poses[0], 24);
    std::memcpy(track_wire + 25, poses[1], 24);
    track_wire[49] = 2;
    std::memcpy(track_wire + 50, poses[2], 24);
    std::memcpy(track_wire + 74, poses[3], 24);
    track_wire[98] = 0x3C;
    Track track;
    size = sizeof track_wire;
    check("track deserialise accepted", track.deserialize(track_wire, &size) == 0 && size == 99);
    check("pair elements are views into the buffer",
          track.pair[0].data() == track_wire + 1 && track.pair[0].size() == 24 &&
              track.pair[1].data() == track_wire + 25 && track.pair[1].size() == 24);
    check("trail keeps its count, elements are views",
          track.trail.size() == 2 && track.trail[1].data() == track_wire + 74 && track.trail[1].size() == 24);
    o = Pose::get_orientation(track.pair[1]);
    check("orientation.y read through pair[1]", Vec3::get_y(o) == 50.0f && track.kind == 0x07 && track.status == 0x3C);
    std::uint8_t track_out[128];
    std::memset(track_out, 0xEE, sizeof track_out);
    out_size = sizeof track_out;
    check("track serialise reproduces the wire",
          track.serialize(track_out, &out_size) == 0 && out_size == 99 && std::memcmp(track_out, track_wire, 99) == 0);
    Track short_track;
    short_size = 37;
    check("short track: pair[1] short, trail empty",
          short_track.deserialize(track_wire, &short_size) == 0 && short_track.pair[1].data() == track_wire + 25 &&
              short_track.pair[1].size() == 12 && short_track.trail.empty() && short_track.status == 0);
    std::memset(track_out, 0xEE, sizeof track_out);
    out_size = sizeof track_out;
    check("short element serialises zero-filled",
          short_track.serialize(track_out, &out_size) == 0 && out_size == 51 &&
              std::memcmp(track_out + 25, track_wire + 25, 12) == 0 && track_out[37] == 0 && track_out[48] == 0 &&
              track_out[49] == 0);
    Track fresh_track;
    check("fresh object holds empty element views",
          fresh_track.pair[0].data() == nullptr && fresh_track.pair[1].empty() && fresh_track.trail.empty());
    std::memset(track_out, 0xEE, sizeof track_out);
    out_size = sizeof track_out;
    zeros    = fresh_track.serialize(track_out, &out_size) == 0 && out_size == 51;
    for (int i = 1; i < 51; ++i)
        zeros = zeros && track_out[i] == 0;
    check("empty element views serialise as zeros", zeros);
    using fixtures_views::vendor::Leading;
    std::uint8_t lead_wire[25];
    std::memset(lead_wire, 0, sizeof lead_wire);
    const float lead_pose[6] = {0.25f, -0.5f, 0.75f, -1.0f, 1.25f, -1.5f};
    std::memcpy(lead_wire, lead_pose, 24);
    lead_wire[24] = 0xA5;
    Leading lead;
    size = sizeof lead_wire;
    check("leading deserialise accepted", lead.deserialize(lead_wire, &size) == 0 && size == 25);
    check("leading view is the buffer itself", lead.pose.data() == lead_wire && lead.pose.size() == 24);
    o = Pose::get_orientation(lead.pose);
    check("orientation.y read through the leading view", Vec3::get_y(o) == 1.25f && lead.status == 0xA5);
    std::memset(out, 0xEE, sizeof out);
    out_size = sizeof out;
    check("leading serialise reproduces the wire",
          lead.serialize(out, &out_size) == 0 && out_size == 25 && std::memcmp(out, lead_wire, 25) == 0);
    Leading short_lead;
    short_size = 12;
    check("short leading view holds what was there",
          short_lead.deserialize(lead_wire, &short_size) == 0 && short_lead.pose.data() == lead_wire &&
              short_lead.pose.size() == 12 && short_lead.status == 0);
    std::memset(out, 0xEE, sizeof out);
    out_size = sizeof out;
    zeros    = short_lead.serialize(out, &out_size) == 0 && out_size == 25 && std::memcmp(out, lead_wire, 12) == 0;
    for (int i = 12; i < 25; ++i)
        zeros = zeros && out[i] == 0;
    check("short leading view serialises zero-filled", zeros);
    Leading empty_lead;
    short_size          = 0;
    const bool empty_ok = empty_lead.deserialize(nullptr, &short_size) == 0 && short_size == 0;
    o                   = Pose::get_orientation(empty_lead.pose);
    check("null buffer leaves an empty leading view",
          empty_ok && empty_lead.pose.empty() && empty_lead.status == 0 && o.empty() && Vec3::get_y(o) == 0.0f);
    Leading fresh_lead;
    check("fresh object holds an empty leading view", fresh_lead.pose.data() == nullptr && fresh_lead.pose.empty());
    std::memset(out, 0xEE, sizeof out);
    out_size = sizeof out;
    zeros    = fresh_lead.serialize(out, &out_size) == 0 && out_size == 25;
    for (int i = 0; i < 25; ++i)
        zeros = zeros && out[i] == 0;
    check("empty leading view serialises as zeros", zeros);
    std::printf("container-views C++: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures == 0 ? 0 : 1;
}
