//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

// Robustness fuzz over the *generated TypeScript deserializers* — the TS analogue
// of the C/Go/Rust decoder fuzzers.
//
// TS/JS is memory-safe and GC'd, so the failure class is neither OOB nor a panic:
// the contract is that arbitrary wire bytes produce either a decoded object or a
// DELIBERATE decode error (a plain `throw new Error(...)`), never a runtime fault
// (TypeError from reading undefined, RangeError from an unbounded `new Array(...)`
// after a length-validation bypass) and never a hang / unbounded allocation. This
// driver feeds a deterministic pseudo-random byte stream (plus edge seeds) into
// each generated deserialize for a compact adversarial fixture set (union,
// variable-length arrays, nested delimited composites, narrow non-byte-aligned
// scalars), round-tripping accepted objects back through serialize.
//
// A RangeError or TypeError escaping deserialize is treated as a defect; a plain
// Error is the expected rejection of malformed input. A hang is caught by the
// harness process timeout.

import { deserializeUni_1_0, serializeUni_1_0 } from "./dsdl/vt/uni_1_0";
import { deserializeOuter_1_0, serializeOuter_1_0 } from "./dsdl/vt/outer_1_0";

// Deterministic xorshift128+ style PRNG (BigInt-free) for reproducible inputs.
let s0 = 0x9e3779b9 >>> 0;
let s1 = 0x243f6a88 >>> 0;
function nextByte(): number {
  let x = s0;
  const y = s1;
  s0 = y;
  x ^= (x << 23) >>> 0;
  x >>>= 0;
  s1 = (x ^ y ^ (x >>> 17) ^ (y >>> 26)) >>> 0;
  return (s1 + y) & 0xff;
}

type Deser = (bytes: Uint8Array) => { value: unknown; consumed: number };
type Ser = (value: never) => Uint8Array;

interface Target {
  readonly name: string;
  readonly deser: Deser;
  readonly ser: Ser;
}

const targets: Target[] = [
  { name: "Uni_1_0", deser: deserializeUni_1_0 as Deser, ser: serializeUni_1_0 as Ser },
  { name: "Outer_1_0", deser: deserializeOuter_1_0 as Deser, ser: serializeOuter_1_0 as Ser },
];

function runOne(target: Target, data: Uint8Array): void {
  let decoded: { value: unknown } | undefined;
  try {
    decoded = target.deser(data);
  } catch (e) {
    // A deliberate decode rejection is a plain Error. A RangeError (e.g. a huge
    // `new Array(...)` from a length-validation bypass) or a TypeError (reading a
    // field off undefined) indicates a real decoder defect.
    if (e instanceof RangeError || e instanceof TypeError) {
      throw new Error(
        `${target.name}: deserialize raised ${(e as Error).constructor.name} ` +
          `on ${data.length} bytes [${Array.from(data).join(",")}]: ${(e as Error).message}`,
      );
    }
    return; // expected: malformed input rejected cleanly
  }
  // Round-trip: re-serializing an accepted object must not fault either.
  try {
    target.ser(decoded.value as never);
  } catch (e) {
    if (e instanceof RangeError || e instanceof TypeError) {
      throw new Error(
        `${target.name}: serialize raised ${(e as Error).constructor.name} after accepting input: ` +
          `${(e as Error).message}`,
      );
    }
    // A plain Error from serialize on an accepted object is also unexpected.
    throw new Error(`${target.name}: serialize threw on an accepted object: ${(e as Error).message}`);
  }
}

function main(): void {
  // Read the iteration override without depending on @types/node's `process`.
  const env = ((globalThis as { process?: { env?: Record<string, string | undefined> } }).process?.env) ?? {};
  const iters = Number.parseInt(env.LLVMDSDL_TS_FUZZ_ITERS ?? "", 10) || 100000;

  const seeds: Uint8Array[] = [
    new Uint8Array([]),
    new Uint8Array([0x00]),
    new Uint8Array([0xff]),
    new Uint8Array(new Array(64).fill(0xff)),
    new Uint8Array(new Array(256).fill(0x00)),
  ];
  for (const seed of seeds) {
    for (const target of targets) {
      runOne(target, seed);
    }
  }

  for (let i = 0; i < iters; ++i) {
    const len = nextByte() | (nextByte() << 8);
    const boundedLen = len % 320; // exceeds the fixtures' max serialized size
    const data = new Uint8Array(boundedLen);
    for (let j = 0; j < boundedLen; ++j) {
      data[j] = nextByte();
    }
    for (const target of targets) {
      runOne(target, data);
    }
  }

  console.log(`ts decoder fuzz: ${iters} random iterations + seeds clean (no runtime fault)`);
}

main();
