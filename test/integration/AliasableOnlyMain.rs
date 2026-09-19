//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading, through the Rust associated-function accessors.
//
//===----------------------------------------------------------------------===//

use aliasable_only_generated::fixtures_aliasable::vendor::pose_1_0::fixtures_aliasable_vendor_Pose as Pose;
use aliasable_only_generated::fixtures_aliasable::vendor::vec3_1_0::fixtures_aliasable_vendor_Vec3 as Vec3;

fn main() {
    let mut buffer = [0u8; 24];
    for (i, v) in [1.5f32, -2.25, 3.0, 4.0, 5.5, 6.75].iter().enumerate() {
        buffer[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
    }

    let orientation = Pose::get_orientation(&buffer);
    let orientation_len = orientation.len();
    let y = Vec3::get_y(orientation);
    let set_result = Vec3::set_z(&mut buffer[..12], 9.5);
    let z = Vec3::get_z(&buffer);
    let short_read = Vec3::get_z(&buffer[..4]);
    let x = Vec3::get_x(Pose::get_position(&buffer));

    let ok = orientation_len == 12
        && y == 5.5
        && set_result == Ok(())
        && z == 9.5
        && short_read == 0.0
        && x == 1.5;
    println!(
        "aliasable-only Rust: {} (orientation {} bytes, y {}, set {:?}, z {}, short {}, x {})",
        if ok { "ok" } else { "FAILED" },
        orientation_len,
        y,
        set_result,
        z,
        short_read,
        x
    );
    std::process::exit(if ok { 0 } else { 1 });
}
