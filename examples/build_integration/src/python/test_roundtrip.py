#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Shared by every Python cell in the matrix.

See src/README.md for what these programs are for and what they deliberately do not test. This one
imports `kitbag` as an installed package and never touches sys.path: what each cell is demonstrating
is that its build backend produced an importable package, and a program that repaired its own import
path would pass whether or not that had happened.

Generated Python types are dataclasses, so comparing a round trip is `==` and needs no field-by-field
spelling out.
"""

from __future__ import annotations

import sys

from kitbag.kitbag.mode_1_0 import Mode_1_0
from kitbag.kitbag.reading_1_0 import Reading_1_0
from kitbag.kitbag.sensor_frame_1_0 import SensorFrame_1_0
from kitbag.uavcan.time.synchronized_timestamp_1_0 import SynchronizedTimestamp_1_0


def main() -> int:
    # Deliberately not the default value: an integration that serialised nothing and deserialised
    # nothing would round-trip a default dataclass perfectly and prove nothing at all.
    original = SensorFrame_1_0(
        timestamp=SynchronizedTimestamp_1_0(microsecond=1_234_567_890_123),
        readings=[
            Reading_1_0(
                channel=n,
                # Exactly representable in binary32, so comparing for equality after the round trip
                # is a statement about the wiring rather than about floating-point rounding.
                value=0.5 * n,
                mode=Mode_1_0(value=2),  # ACTIVE
            )
            for n in (1, 2, 3)
        ],
        source=list(b"imu.0"),
    )

    wire = original.serialize()
    restored = SensorFrame_1_0.deserialize(wire)

    if restored != original:
        print(f"FAIL: round-trip changed the value\n  sent:     {original}\n  received: {restored}",
              file=sys.stderr)
        return 1

    print(f"round-trip OK: {len(restored.readings)} readings, "
          f"{len(restored.source)} source bytes, {len(wire)} bytes on the wire")
    return 0


if __name__ == "__main__":
    sys.exit(main())
