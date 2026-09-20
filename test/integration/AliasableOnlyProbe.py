#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# The C probe's reading, through the Python static-method accessors.
#
#===----------------------------------------------------------------------===#

import importlib
import struct
import sys


def main() -> int:
    package_root, package = sys.argv[1], sys.argv[2]
    sys.path.insert(0, package_root)
    pose = importlib.import_module(f"{package}.fixtures_aliasable.vendor.pose_1_0").Pose
    vec3 = importlib.import_module(f"{package}.fixtures_aliasable.vendor.vec3_1_0").Vec3

    buffer = memoryview(bytearray(struct.pack("<6f", 1.5, -2.25, 3.0, 4.0, 5.5, 6.75)))
    orientation = pose.get_orientation(buffer)
    y = vec3.get_y(orientation)
    set_result = vec3.set_z(buffer[:12], 9.5)
    z = vec3.get_z(buffer)
    short_read = vec3.get_z(buffer[:4])
    x = vec3.get_x(pose.get_position(buffer))

    ok = len(orientation) == 12 and y == 5.5 and set_result == 0 and z == 9.5 and short_read == 0 and x == 1.5
    print(
        f"aliasable-only Python: {'ok' if ok else 'FAILED'} (orientation {len(orientation)} bytes, y {y}, "
        f"set {set_result}, z {z}, short {short_read}, x {x})"
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
