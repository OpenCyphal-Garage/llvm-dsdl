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
    println!("container-views Rust: {}", if failures == 0 { "ok" } else { "FAILED" });
    std::process::exit(if failures == 0 { 0 } else { 1 });
}
