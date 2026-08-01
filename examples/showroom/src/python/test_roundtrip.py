#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Shared by every Python recipe in the matrix.

See src/README.md for what these programs are for and what they deliberately do not test. This one
imports `lanyard` as an installed package and never touches sys.path: what each recipe is
demonstrating is that its build backend produced an importable package, and a program that repaired
its own import path would pass whether or not that had happened.

Generated Python types are dataclasses, so comparing a round trip is `==` and needs no field-by-field
spelling out.
"""

from __future__ import annotations

import sys

from lanyard.lanyard.health.subsystem_report_1_0 import SubsystemReport_1_0
from lanyard.lanyard.health.system_health_1_0 import SystemHealth_1_0
from lanyard.uavcan.diagnostic.severity_1_0 import Severity_1_0
from lanyard.uavcan.node.health_1_0 import Health_1_0
from lanyard.uavcan.time.synchronized_timestamp_1_0 import SynchronizedTimestamp_1_0

SUBSYSTEMS = ("gnss", "esc.3", "imu.0")


def main() -> int:
    # Deliberately not the default value: an integration that serialised nothing and deserialised
    # nothing would round-trip a default dataclass perfectly and prove nothing at all.
    original = SystemHealth_1_0(
        timestamp=SynchronizedTimestamp_1_0(microsecond=1_234_567_890_123),
        aggregate_health=Health_1_0(value=2),  # CAUTION, on the standard four-level scale
        subsystem=[
            SubsystemReport_1_0(
                health=Health_1_0(value=index % 4),
                severity=Severity_1_0(value=index % 8),
                fault_code=0x1000 + index,
                name=list(name.encode()),
            )
            for index, name in enumerate(SUBSYSTEMS)
        ],
    )

    wire = original.serialize()
    restored = SystemHealth_1_0.deserialize(wire)

    if restored != original:
        print(f"FAIL: round-trip changed the value\n  sent:     {original}\n  received: {restored}",
              file=sys.stderr)
        return 1

    print(f"round-trip OK: {len(restored.subsystem)} subsystems, {len(wire)} bytes on the wire")
    return 0


if __name__ == "__main__":
    sys.exit(main())
