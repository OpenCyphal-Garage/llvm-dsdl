// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
// Shared by every TypeScript cell in the matrix. See src/README.md for what these programs are for
// and what they deliberately do not test.
//
// The import is a relative path into the generated tree rather than a package specifier, and that is
// the whole TypeScript story in one line -- see the cell pages for why. dsdlc emits TypeScript
// *source*, so it joins your program as source.

import {
  deserializeSensorFrame_1_0,
  serializeSensorFrame_1_0,
  type SensorFrame_1_0,
} from "../../generated/kitbag/sensor_frame_1_0";

// Throwing rather than process.exit: an uncaught exception is already a non-zero exit with a stack
// trace attached, and it keeps this program free of anything Node-specific.
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
const original: SensorFrame_1_0 = {
  timestamp: { microsecond: 1_234_567_890_123n },
  readings: [1, 2, 3].map((n) => ({
    channel: n,
    // Exactly representable in binary64 and in binary32, so comparing for equality after the round
    // trip is a statement about the wiring rather than about floating-point rounding.
    value: 0.5 * n,
    mode: { value: 2 }, // ACTIVE
  })),
  source: [..."imu.0"].map((c) => c.charCodeAt(0)),
};

const bytes = serializeSensorFrame_1_0(original);
const { value: restored } = deserializeSensorFrame_1_0(bytes);

if (show(restored) !== show(original)) {
  fail(`round-trip changed the value\n  sent:     ${show(original)}\n  received: ${show(restored)}`);
}

console.log(
  `round-trip OK: ${restored.readings.length} readings, ` +
    `${restored.source.length} source bytes, ${bytes.length} bytes on the wire`,
);
