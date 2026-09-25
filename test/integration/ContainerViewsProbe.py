#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# The C probe's reading of a container holding a view, in Python: the view is a memoryview of
# the buffer, and a write to the buffer shows through it.
#
#===----------------------------------------------------------------------===#

import struct, sys
sys.path.insert(0, sys.argv[1])
from dsdl_gen.fixtures_views.vendor.frame_1_0 import Frame
from dsdl_gen.fixtures_views.vendor.leading_1_0 import Leading
from dsdl_gen.fixtures_views.vendor.track_1_0 import Track
from dsdl_gen.fixtures_aliasable.vendor.pose_1_0 import Pose
from dsdl_gen.fixtures_aliasable.vendor.vec3_1_0 import Vec3
failures = 0
def check(what, ok):
    global failures
    print(f"  {what:<44} {'ok' if ok else 'FAILED'}")
    if not ok: failures += 1
wire = bytearray(41)
wire[0:4] = struct.pack("<I", 0x11223344)
wire[4:28] = struct.pack("<6f", 1.5, -2.25, 3.0, 4.0, 5.5, 6.75)
wire[28:40] = struct.pack("<3f", 7.0, 8.0, 9.0)
wire[40] = 0x5A
frame = Frame.deserialize(memoryview(wire))
check("sequence decoded", frame.sequence == 0x11223344)
check("view points into the buffer", frame.pose.obj is wire and frame.pose.nbytes == 24 and bytes(frame.pose) == bytes(wire[4:28]))
wire[4] ^= 0xFF
check("view is live, not a copy", frame.pose[0] == wire[4])
wire[4] ^= 0xFF
check("orientation.y read through the view", Vec3.get_y(Pose.get_orientation(frame.pose)) == 5.5)
check("velocity decoded, status after the view", frame.velocity.y == 8.0 and frame.status == 0x5A)
check("serialise reproduces the wire", frame.serialize() == bytes(wire))
short = Frame.deserialize(memoryview(wire)[:16])
check("short view holds what was there", short.pose.nbytes == 12 and bytes(short.pose) == bytes(wire[4:16]))
o = Pose.get_orientation(short.pose)
check("missing orientation reads as zero", len(o) == 0 and Vec3.get_y(o) == 0 and short.status == 0)
short_out = short.serialize()
check("short view serialises zero-filled", short_out[4:16] == bytes(wire[4:16]) and short_out[16:28] == bytes(12))
fresh = Frame()
check("fresh object holds an empty view", fresh.pose.nbytes == 0)
check("empty view serialises as zeros", fresh.serialize()[4:28] == bytes(24))
track_wire = bytearray(99)
track_wire[0] = 0x07
for at, pose in zip((1, 25, 50, 74), ((1, 2, 3, 4, 5, 6), (10, 20, 30, 40, 50, 60), (-1, -2, -3, -4, -5, -6), (0.5, 1.5, 2.5, 3.5, 4.5, 5.5))):
    track_wire[at:at + 24] = struct.pack("<6f", *pose)
track_wire[49] = 2
track_wire[98] = 0x3C
track = Track.deserialize(memoryview(track_wire))
check("pair elements are views into the buffer", all(p.obj is track_wire for p in track.pair) and bytes(track.pair[0]) == bytes(track_wire[1:25]) and bytes(track.pair[1]) == bytes(track_wire[25:49]))
check("trail keeps its count, elements are views", len(track.trail) == 2 and track.trail[1].obj is track_wire and bytes(track.trail[1]) == bytes(track_wire[74:98]))
track_wire[25] ^= 0xFF
check("element view is live, not a copy", track.pair[1][0] == track_wire[25])
track_wire[25] ^= 0xFF
check("orientation.y read through pair[1]", Vec3.get_y(Pose.get_orientation(track.pair[1])) == 50.0 and track.kind == 0x07 and track.status == 0x3C)
check("track serialise reproduces the wire", track.serialize() == bytes(track_wire))
short_track = Track.deserialize(memoryview(track_wire)[:37])
check("short track: pair[1] short, trail empty", short_track.pair[1].nbytes == 12 and bytes(short_track.pair[1]) == bytes(track_wire[25:37]) and len(short_track.trail) == 0 and short_track.status == 0)
short_track_out = short_track.serialize()
check("short element serialises zero-filled", len(short_track_out) == 51 and short_track_out[25:37] == bytes(track_wire[25:37]) and short_track_out[37:51] == bytes(14))
fresh_track = Track()
check("fresh object holds empty element views", len(fresh_track.pair) == 2 and all(p.nbytes == 0 for p in fresh_track.pair) and len(fresh_track.trail) == 0)
check("empty element views serialise as zeros", fresh_track.serialize() == bytes(51))
lead_wire = bytearray(25)
lead_wire[0:24] = struct.pack("<6f", 0.25, -0.5, 0.75, -1.0, 1.25, -1.5)
lead_wire[24] = 0xA5
lead = Leading.deserialize(memoryview(lead_wire))
check("leading view is the buffer itself", lead.pose.obj is lead_wire and lead.pose.nbytes == 24 and bytes(lead.pose) == bytes(lead_wire[:24]))
lead_wire[0] ^= 0xFF
check("leading view is live, not a copy", lead.pose[0] == lead_wire[0])
lead_wire[0] ^= 0xFF
check("orientation.y read through the leading view", Vec3.get_y(Pose.get_orientation(lead.pose)) == 1.25 and lead.status == 0xA5)
check("leading serialise reproduces the wire", lead.serialize() == bytes(lead_wire))
short_lead = Leading.deserialize(memoryview(lead_wire)[:12])
check("short leading view holds what was there", short_lead.pose.obj is lead_wire and short_lead.pose.nbytes == 12 and short_lead.status == 0)
short_lead_out = short_lead.serialize()
check("short leading view serialises zero-filled", len(short_lead_out) == 25 and short_lead_out[:12] == bytes(lead_wire[:12]) and short_lead_out[12:] == bytes(13))
empty_lead = Leading.deserialize(memoryview(b""))
o = Pose.get_orientation(empty_lead.pose)
check("empty buffer leaves an empty leading view", empty_lead.pose.nbytes == 0 and empty_lead.status == 0 and len(o) == 0 and Vec3.get_y(o) == 0)
fresh_lead = Leading()
check("fresh object holds an empty leading view", fresh_lead.pose.nbytes == 0)
check("empty leading view serialises as zeros", fresh_lead.serialize() == bytes(25))
print(f"container-views Python: {'ok' if failures == 0 else 'FAILED'}")
sys.exit(0 if failures == 0 else 1)
