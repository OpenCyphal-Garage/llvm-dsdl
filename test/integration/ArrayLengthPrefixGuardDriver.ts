//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Array-length prefixes decoded by the generated TypeScript: above the capacity, beyond the
// index width, and at the capacity. One line per case and a summary line the lane parses. An
// Array holds at most 2^32 - 1 elements, so the cases beyond a 32-bit index are rejected here.
//
//===----------------------------------------------------------------------===//

import * as prefix32 from "./prefixguard/prefix32_1_0";
import * as prefix64 from "./prefixguard/prefix64_1_0";

declare const process: { exit(code: number): never };

// The code a body answers with for an array length outside the declared capacity.
const BAD_ARRAY_LENGTH = -10;

let passed = 0;
let skipped = 0;
let failed = 0;

function outcome(verdict: "PASS" | "SKIP" | "FAIL", name: string, prefix: bigint, detail: string): void {
  if (verdict === "PASS") {
    passed += 1;
  } else if (verdict === "SKIP") {
    skipped += 1;
  } else {
    failed += 1;
  }
  if (detail === "") {
    console.log(`${verdict} ${name} prefix=${prefix}`);
  } else if (verdict === "FAIL") {
    console.log(`${verdict} ${name} prefix=${prefix}: ${detail}`);
  } else {
    console.log(`${verdict} ${name} prefix=${prefix} ${detail}`);
  }
}

// A buffer opening with a little-endian prefix of prefixBytes bytes followed by payloadBytes zero
// bytes.
function withPrefix(prefix: bigint, prefixBytes: number, payloadBytes: number): Uint8Array {
  const buffer = new Uint8Array(prefixBytes + payloadBytes);
  for (let i = 0; i < prefixBytes; i += 1) {
    buffer[i] = Number((prefix >> BigInt(8 * i)) & 0xffn);
  }
  return buffer;
}

// A decode that has to fail with a bad array length and leave the array empty. A body that throws
// instead of answering a code is a failure with the exception's text.
function expectRejected(name: string, prefix: bigint, decode: () => number, lengthAfter: () => number): void {
  let rc: number;
  try {
    rc = decode();
  } catch (error) {
    outcome("FAIL", name, prefix, `threw ${String(error)}`);
    return;
  }
  if (rc !== BAD_ARRAY_LENGTH) {
    outcome("FAIL", name, prefix, `rc = ${rc}, want ${BAD_ARRAY_LENGTH}`);
    return;
  }
  if (lengthAfter() !== 0) {
    outcome("FAIL", name, prefix, `array holds ${lengthAfter()} elements after rejection`);
    return;
  }
  outcome("PASS", name, prefix, "");
}

function prefix32RejectsAboveCapacity(prefix: number): void {
  const obj: prefix32.Prefix32@V1_0@ = { payload: [] };
  const buffer = withPrefix(BigInt(prefix), 4, 0);
  expectRejected(
    "prefix32_rejects_above_capacity",
    BigInt(prefix),
    () => prefix32.deserializePrefix32@V1_0@From(obj, buffer),
    () => obj.payload.length,
  );
}

function prefix32AcceptsCapacity(): void {
  const capacity = 65536;
  const obj: prefix32.Prefix32@V1_0@ = { payload: [] };
  const buffer = withPrefix(BigInt(capacity), 4, capacity);
  let rc: number;
  try {
    rc = prefix32.deserializePrefix32@V1_0@From(obj, buffer);
  } catch (error) {
    outcome("FAIL", "prefix32_accepts_capacity", BigInt(capacity), `threw ${String(error)}`);
    return;
  }
  if (rc !== buffer.length) {
    outcome("FAIL", "prefix32_accepts_capacity", BigInt(capacity), `rc = ${rc}, want ${buffer.length} bytes consumed`);
  } else if (obj.payload.length !== capacity) {
    outcome("FAIL", "prefix32_accepts_capacity", BigInt(capacity), `payload holds ${obj.payload.length} elements, want ${capacity}`);
  } else {
    outcome("PASS", "prefix32_accepts_capacity", BigInt(capacity), "");
  }
}

function prefix64RejectsAboveCapacity(prefix: bigint): void {
  const obj: prefix64.Prefix64@V1_0@ = { flags: [] };
  const buffer = withPrefix(prefix, 8, 0);
  expectRejected(
    "prefix64_rejects_above_capacity",
    prefix,
    () => prefix64.deserializePrefix64@V1_0@From(obj, buffer),
    () => obj.flags.length,
  );
}

function prefix64RejectsBeyondIndex(prefix: bigint): void {
  const obj: prefix64.Prefix64@V1_0@ = { flags: [] };
  const buffer = withPrefix(prefix, 8, 0);
  expectRejected(
    "prefix64_rejects_beyond_index",
    prefix,
    () => prefix64.deserializePrefix64@V1_0@From(obj, buffer),
    () => obj.flags.length,
  );
}

function prefix64AcceptsSmallLength(): void {
  const obj: prefix64.Prefix64@V1_0@ = { flags: [] };
  const buffer = withPrefix(3n, 8, 1);
  buffer[8] = 0x05;
  let rc: number;
  try {
    rc = prefix64.deserializePrefix64@V1_0@From(obj, buffer);
  } catch (error) {
    outcome("FAIL", "prefix64_accepts_small_length", 3n, `threw ${String(error)}`);
    return;
  }
  const want = [true, false, true];
  if (rc !== buffer.length) {
    outcome("FAIL", "prefix64_accepts_small_length", 3n, `rc = ${rc}, want ${buffer.length} bytes consumed`);
  } else if (obj.flags.length !== want.length || want.some((flag, i) => obj.flags[i] !== flag)) {
    outcome("FAIL", "prefix64_accepts_small_length", 3n, `flags = ${JSON.stringify(obj.flags)}, want ${JSON.stringify(want)}`);
  } else {
    outcome("PASS", "prefix64_accepts_small_length", 3n, "");
  }
}

for (const prefix of [65537, 2 ** 31, 2 ** 32 - 1]) {
  prefix32RejectsAboveCapacity(prefix);
}
prefix32AcceptsCapacity();
for (const prefix of [(1n << 33n) + 1n, (1n << 33n) + 3n, 1n << 63n, (1n << 64n) - 1n]) {
  prefix64RejectsAboveCapacity(prefix);
}
for (const prefix of [1n << 32n, (1n << 32n) + 3n, 1n << 33n]) {
  prefix64RejectsBeyondIndex(prefix);
}
prefix64AcceptsSmallLength();

const status = failed === 0 ? "PASS" : "FAIL";
console.log(
  `${status} ts-array-length-prefix-guard cases=${passed + skipped + failed} passed=${passed} skipped=${skipped} failed=${failed}`,
);
process.exit(failed === 0 ? 0 : 1);
