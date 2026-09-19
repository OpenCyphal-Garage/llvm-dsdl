//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading of a container holding a view, in Rust: the holder borrows the buffer for its lifetime.
//
//===----------------------------------------------------------------------===//

use llvmdsdl_generated::fixtures_aliasable::vendor::pose_1_0::fixtures_aliasable_vendor_Pose as Pose;
use llvmdsdl_generated::fixtures_aliasable::vendor::vec3_1_0::fixtures_aliasable_vendor_Vec3 as Vec3;
use llvmdsdl_generated::fixtures_views::vendor::frame_1_0::fixtures_views_vendor_Frame as Frame;
use llvmdsdl_generated::fixtures_views::vendor::track_1_0::fixtures_views_vendor_Track as Track;
fn main() {
    let mut failures = 0;
    let mut check = |what: &str, ok: bool| { println!("  {:<44} {}", what, if ok { "ok" } else { "FAILED" }); if !ok { failures += 1; } };
    let mut wire = [0u8; 41];
    wire[0..4].copy_from_slice(&0x11223344u32.to_le_bytes());
    for (i, v) in [1.5f32, -2.25, 3.0, 4.0, 5.5, 6.75].iter().enumerate() { wire[4 + i * 4..8 + i * 4].copy_from_slice(&v.to_le_bytes()); }
    for (i, v) in [7.0f32, 8.0, 9.0].iter().enumerate() { wire[28 + i * 4..32 + i * 4].copy_from_slice(&v.to_le_bytes()); }
    wire[40] = 0x5A;
    let mut frame = Frame::default();
    check("deserialise accepted", frame.deserialize(&wire) == Ok(41));
    check("sequence decoded", frame.sequence == 0x11223344);
    check("view points into the buffer", frame.pose.as_ptr() == wire[4..].as_ptr() && frame.pose.len() == 24);
    check("orientation.y read through the view", Vec3::get_y(Pose::get_orientation(frame.pose)) == 5.5);
    check("velocity decoded, status after the view", frame.velocity.y == 8.0 && frame.status == 0x5A);
    let mut out = [0xEEu8; 64];
    check("serialise reproduces the wire", frame.serialize(&mut out) == Ok(41) && out[..41] == wire);
    let mut short = Frame::default();
    check("short deserialise accepted", short.deserialize(&wire[..16]).is_ok());
    check("short view holds what was there", short.pose.len() == 12 && short.pose.as_ptr() == wire[4..].as_ptr());
    let o = Pose::get_orientation(short.pose);
    check("missing orientation reads as zero", o.is_empty() && Vec3::get_y(o) == 0.0 && short.status == 0);
    let mut out = [0xEEu8; 64];
    check("short view serialises zero-filled", short.serialize(&mut out).is_ok() && out[4..16] == wire[4..16] && out[16..28].iter().all(|&b| b == 0));
    let fresh = Frame::default();
    check("fresh object holds an empty view", fresh.pose.is_empty());
    let mut out = [0xEEu8; 64];
    check("empty view serialises as zeros", fresh.serialize(&mut out).is_ok() && out[4..28].iter().all(|&b| b == 0));
    let mut track_wire = [0u8; 99];
    track_wire[0] = 0x07;
    let poses: [[f32; 6]; 4] = [[1.0, 2.0, 3.0, 4.0, 5.0, 6.0], [10.0, 20.0, 30.0, 40.0, 50.0, 60.0], [-1.0, -2.0, -3.0, -4.0, -5.0, -6.0], [0.5, 1.5, 2.5, 3.5, 4.5, 5.5]];
    for (at, pose) in [1usize, 25, 50, 74].iter().zip(poses.iter()) { for (i, v) in pose.iter().enumerate() { track_wire[at + i * 4..at + i * 4 + 4].copy_from_slice(&v.to_le_bytes()); } }
    track_wire[49] = 2;
    track_wire[98] = 0x3C;
    let mut track = Track::default();
    check("track deserialise accepted", track.deserialize(&track_wire) == Ok(99));
    check("pair elements are views into the buffer", track.pair[0].as_ptr() == track_wire[1..].as_ptr() && track.pair[0].len() == 24 && track.pair[1].as_ptr() == track_wire[25..].as_ptr() && track.pair[1].len() == 24);
    check("trail keeps its count, elements are views", track.trail.len() == 2 && track.trail[1].as_ptr() == track_wire[74..].as_ptr() && track.trail[1].len() == 24);
    check("orientation.y read through pair[1]", Vec3::get_y(Pose::get_orientation(track.pair[1])) == 50.0 && track.kind == 0x07 && track.status == 0x3C);
    let mut out = [0xEEu8; 128];
    check("track serialise reproduces the wire", track.serialize(&mut out) == Ok(99) && out[..99] == track_wire);
    let mut short_track = Track::default();
    check("short track: pair[1] short, trail empty", short_track.deserialize(&track_wire[..37]).is_ok() && short_track.pair[1].as_ptr() == track_wire[25..].as_ptr() && short_track.pair[1].len() == 12 && short_track.trail.len() == 0 && short_track.status == 0);
    let mut out = [0xEEu8; 128];
    check("short element serialises zero-filled", short_track.serialize(&mut out) == Ok(51) && out[25..37] == track_wire[25..37] && out[37..51].iter().all(|&b| b == 0));
    let fresh_track = Track::default();
    check("fresh object holds empty element views", fresh_track.pair.iter().all(|p| p.is_empty()) && fresh_track.trail.len() == 0);
    let mut out = [0xEEu8; 128];
    check("empty element views serialise as zeros", fresh_track.serialize(&mut out) == Ok(51) && out[1..51].iter().all(|&b| b == 0));
    println!("container-views Rust: {}", if failures == 0 { "ok" } else { "FAILED" });
    std::process::exit(if failures == 0 { 0 } else { 1 });
}
