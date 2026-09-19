//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// A container holding a view of an @aliasable record, deserialised and serialised.
//
// The view points into the buffer and holds what the buffer held of the record; the record's own
// accessors read it; serialising copies it back and reproduces the wire. A buffer that ends inside
// the record leaves a short view, read as zeros past its end and serialised zero-filled; an
// initialised object holds an empty view, serialised as the record's zeros. An array of the record
// is one view per element, the fixed pair's held in place and the bounded trail's beside its count.
// Compiled against the generated output and nothing else, for C and for the object target.
//
//===----------------------------------------------------------------------===//

#include "fixtures_aliasable/vendor/Pose_1_0.h"
#include "fixtures_aliasable/vendor/Vec3_1_0.h"
#include "fixtures_views/vendor/Frame_1_0.h"
#include "fixtures_views/vendor/Track_1_0.h"
#include <stdio.h>
#include <string.h>

/* Frame: uint32 sequence | Pose (24 bytes: 6 x float32) | Vec3 (12 bytes) | uint8 status = 41 bytes. */
static int check(const char* const what, const int ok, int* const failures)
{
    printf("  %-44s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
    {
        ++*failures;
    }
    return ok;
}

int main(void)
{
    int     failures = 0;
    uint8_t wire[41];
    memset(wire, 0, sizeof wire);
    const uint32_t seq = 0x11223344U;
    memcpy(wire, &seq, 4);
    const float pose[6] = {1.5f, -2.25f, 3.0f, 4.0f, 5.5f, 6.75f};
    memcpy(wire + 4, pose, 24);
    const float vel[3] = {7.0f, 8.0f, 9.0f};
    memcpy(wire + 28, vel, 12);
    wire[40] = 0x5A;

    fixtures_views__vendor__Frame frame;
    size_t                        size = sizeof wire;
    int8_t                        rc   = fixtures_views__vendor__Frame__deserialize_(&frame, wire, &size);
    check("deserialise accepted", rc == 0 && size == sizeof wire, &failures);
    check("sequence decoded", frame.sequence == seq, &failures);
    check("view points into the buffer", frame.pose.bytes == wire + 4, &failures);
    check("view holds the whole record", frame.pose.size_bytes == 24, &failures);
    size_t         n = 0;
    const uint8_t* o = fixtures_aliasable__vendor__Pose__get_orientation_(frame.pose.bytes, frame.pose.size_bytes, &n);
    check("orientation.y read through the view", fixtures_aliasable__vendor__Vec3__get_y_(o, n) == 5.5f, &failures);
    check("velocity decoded as a copy", frame.velocity.y == 8.0f, &failures);
    check("status decoded after the view", frame.status == 0x5A, &failures);

    uint8_t out[64];
    memset(out, 0xEE, sizeof out);
    size_t out_size = sizeof out;
    rc              = fixtures_views__vendor__Frame__serialize_(&frame, out, &out_size);
    check("serialise from the view accepted", rc == 0 && out_size == sizeof wire, &failures);
    check("serialise reproduces the wire", memcmp(out, wire, sizeof wire) == 0, &failures);

    /* A buffer that ends inside the Pose: the view is short, the accessors zero-extend. */
    fixtures_views__vendor__Frame short_frame;
    size_t                        short_size = 16; /* sequence + 12 bytes of the Pose */
    rc = fixtures_views__vendor__Frame__deserialize_(&short_frame, wire, &short_size);
    check("short deserialise accepted", rc == 0, &failures);
    check("short view holds what was there",
          short_frame.pose.bytes == wire + 4 && short_frame.pose.size_bytes == 12,
          &failures);
    o = fixtures_aliasable__vendor__Pose__get_orientation_(short_frame.pose.bytes, short_frame.pose.size_bytes, &n);
    check("missing orientation reads as zero",
          n == 0 && fixtures_aliasable__vendor__Vec3__get_y_(o, n) == 0.0f,
          &failures);
    check("field after a short view is zero", short_frame.status == 0 && short_frame.velocity.x == 0.0f, &failures);
    memset(out, 0xEE, sizeof out);
    out_size = sizeof out;
    rc       = fixtures_views__vendor__Frame__serialize_(&short_frame, out, &out_size);
    check("short view serialises zero-filled",
          rc == 0 && memcmp(out + 4, wire + 4, 12) == 0 && out[16] == 0 && out[27] == 0,
          &failures);

    /* An initialised object holds an empty view, which serialises as the record's zeros. */
    fixtures_views__vendor__Frame fresh;
    rc = fixtures_views__vendor__Frame__initialize_(&fresh);
    check("initialise clears the view", rc == 0 && fresh.pose.bytes == NULL && fresh.pose.size_bytes == 0, &failures);
    memset(out, 0xEE, sizeof out);
    out_size  = sizeof out;
    rc        = fixtures_views__vendor__Frame__serialize_(&fresh, out, &out_size);
    int zeros = 1;
    for (size_t i = 4; i < 28; ++i)
    {
        zeros = zeros && (out[i] == 0);
    }
    check("empty view serialises as zeros", rc == 0 && zeros, &failures);

    /* Track: uint8 kind | Pose[2] pair (48) | uint8 count | Pose[<=3] trail | uint8 status: 99 bytes with two. */
    uint8_t track_wire[99];
    memset(track_wire, 0, sizeof track_wire);
    track_wire[0]           = 0x07;
    const float poses[4][6] = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
                               {10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f},
                               {-1.0f, -2.0f, -3.0f, -4.0f, -5.0f, -6.0f},
                               {0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f}};
    memcpy(track_wire + 1, poses[0], 24);
    memcpy(track_wire + 25, poses[1], 24);
    track_wire[49] = 2;
    memcpy(track_wire + 50, poses[2], 24);
    memcpy(track_wire + 74, poses[3], 24);
    track_wire[98] = 0x3C;

    fixtures_views__vendor__Track track;
    size = sizeof track_wire;
    rc   = fixtures_views__vendor__Track__deserialize_(&track, track_wire, &size);
    check("track deserialise accepted", rc == 0 && size == sizeof track_wire, &failures);
    check("pair elements are views into the buffer",
          track.pair[0].bytes == track_wire + 1 && track.pair[0].size_bytes == 24 &&
              track.pair[1].bytes == track_wire + 25 && track.pair[1].size_bytes == 24,
          &failures);
    check("trail keeps its count, elements are views",
          track.trail.count == 2 && track.trail.elements[1].bytes == track_wire + 74 &&
              track.trail.elements[1].size_bytes == 24,
          &failures);
    o = fixtures_aliasable__vendor__Pose__get_orientation_(track.pair[1].bytes, track.pair[1].size_bytes, &n);
    check("orientation.y read through pair[1]", fixtures_aliasable__vendor__Vec3__get_y_(o, n) == 50.0f, &failures);
    check("fields around the arrays decoded", track.kind == 0x07 && track.status == 0x3C, &failures);
    uint8_t track_out[128];
    memset(track_out, 0xEE, sizeof track_out);
    out_size = sizeof track_out;
    rc       = fixtures_views__vendor__Track__serialize_(&track, track_out, &out_size);
    check("track serialise reproduces the wire",
          rc == 0 && out_size == sizeof track_wire && memcmp(track_out, track_wire, sizeof track_wire) == 0,
          &failures);

    /* A buffer that ends inside pair[1]: that element is short, and the trail is empty. */
    fixtures_views__vendor__Track short_track;
    short_size = 37;
    rc         = fixtures_views__vendor__Track__deserialize_(&short_track, track_wire, &short_size);
    check("short track: pair[1] short, trail empty",
          rc == 0 && short_track.pair[1].bytes == track_wire + 25 && short_track.pair[1].size_bytes == 12 &&
              short_track.trail.count == 0 && short_track.status == 0,
          &failures);
    memset(track_out, 0xEE, sizeof track_out);
    out_size = sizeof track_out;
    rc       = fixtures_views__vendor__Track__serialize_(&short_track, track_out, &out_size);
    check("short element serialises zero-filled",
          rc == 0 && out_size == 51 && memcmp(track_out + 25, track_wire + 25, 12) == 0 && track_out[37] == 0 &&
              track_out[48] == 0 && track_out[49] == 0,
          &failures);

    /* An initialised object holds an empty view in each element of the pair, and an empty trail. */
    fixtures_views__vendor__Track fresh_track;
    rc = fixtures_views__vendor__Track__initialize_(&fresh_track);
    check("initialise clears every element view",
          rc == 0 && fresh_track.pair[0].bytes == NULL && fresh_track.pair[0].size_bytes == 0 &&
              fresh_track.pair[1].bytes == NULL && fresh_track.pair[1].size_bytes == 0 && fresh_track.trail.count == 0,
          &failures);
    memset(track_out, 0xEE, sizeof track_out);
    out_size = sizeof track_out;
    rc       = fixtures_views__vendor__Track__serialize_(&fresh_track, track_out, &out_size);
    zeros    = (rc == 0) && (out_size == 51);
    for (size_t i = 1; i < 51; ++i)
    {
        zeros = zeros && (track_out[i] == 0);
    }
    check("empty element views serialise as zeros", zeros, &failures);

    printf("container-views %s: %s\n", TARGET_NAME, failures == 0 ? "ok" : "FAILED");
    return failures == 0 ? 0 : 1;
}
