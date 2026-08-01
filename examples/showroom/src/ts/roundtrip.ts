// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
// Shared by every TypeScript recipe in the matrix. See src/README.md for what these programs are for
// and what they deliberately do not test.
//
// The import is a relative path into the generated tree rather than a package specifier, and that is
// the whole TypeScript story in one line -- see the recipe pages for why. dsdlc emits TypeScript
// *source*, so it joins your program as source.

import {
  deserializeSystemHealth_1_0,
  serializeSystemHealth_1_0,
  type SystemHealth_1_0,
} from "../../generated/lanyard/health/system_health_1_0";
import type { SubsystemReport_1_0 } from "../../generated/lanyard/health/subsystem_report_1_0";

const SUBSYSTEMS = ["gnss", "esc.3", "imu.0"];

function fail(message: string): never {
  throw new Error(`FAIL: ${message}`);
}

// JSON.stringify with bigint support, used only to render a mismatch. The timestamp is a 56-bit
// field and arrives as a bigint, which JSON.stringify refuses to serialise on its own.
function show(value: unknown): string {
  return JSON.stringify(value, (_key, v) => (typeof v === "bigint" ? v.toString() : v));
}

// Deliberately not an empty value: an integration that serialised nothing and deserialised nothing
// would round-trip an empty object perfectly and prove nothing at all.
const original: SystemHealth_1_0 = {
  timestamp: { microsecond: 1_234_567_890_123n },
  aggregate_health: { value: 2 }, // CAUTION, on the standard four-level scale
  subsystem: SUBSYSTEMS.map((name, index): SubsystemReport_1_0 => ({
    health: { value: index % 4 },
    severity: { value: index % 8 },
    fault_code: 0x1000 + index,
    name: [...name].map((c) => c.charCodeAt(0)),
  })),
};

const bytes = serializeSystemHealth_1_0(original);
const { value: restored } = deserializeSystemHealth_1_0(bytes);

if (show(restored) !== show(original)) {
  fail(`round-trip changed the value\n  sent:     ${show(original)}\n  received: ${show(restored)}`);
}

console.log(
  `round-trip OK: ${restored.subsystem.length} subsystems, ${bytes.length} bytes on the wire`,
);
