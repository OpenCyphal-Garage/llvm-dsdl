//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Isolated cross-language runtime-primitive equivalence driver (TypeScript).
//
// Reads the shared vector file (argv[2]) and exercises each vector directly
// against the generated TypeScript runtime primitives, checking every primitive
// direction on its own. Prints "PROCESSED <n>" and "SKIPPED <n>" and exits
// non-zero on the first genuine mismatch.
//
// TypeScript numbers are IEEE-754 doubles, so float16 *unpack* results that are
// NaN are SKIPPED with a printed reason (never silently): reading a binary16 NaN
// into a JS number canonicalizes the payload, so the exact float32 NaN bit
// pattern cannot be reproduced. float16 pack, integer reads, and copy_bits are
// exact and fully checked.
//
//===----------------------------------------------------------------------===//

import {
  writeFloat,
  readFloat,
  readSignedBigInt,
  readUnsignedBigInt,
  copyBits,
} from "./dsdl_runtime";

// Minimal ambient declarations for the handful of Node globals used below, so
// the driver compiles without an @types/node dependency.
declare function require(moduleName: string): any;
declare const process: { argv: string[]; exit(code: number): never };
const fs: { readFileSync(path: string, encoding: string): string } = require("fs");

function toHex(bytes: Uint8Array): string {
  let out = "";
  for (let i = 0; i < bytes.length; ++i) {
    out += bytes[i].toString(16).padStart(2, "0");
  }
  return out;
}

function f32FromBits(hex: string): number {
  const u = new Uint32Array(1);
  u[0] = parseInt(hex, 16) >>> 0;
  return new Float32Array(u.buffer)[0];
}

function f32ToBits(value: number): number {
  const f = new Float32Array(1);
  f[0] = value;
  return new Uint32Array(f.buffer)[0] >>> 0;
}

function hexBytes(hex: string): Uint8Array {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < hex.length; i += 2) {
    out[i / 2] = parseInt(hex.substr(i, 2), 16);
  }
  return out;
}

function u16LeBytes(value: number): Uint8Array {
  return new Uint8Array([value & 0xff, (value >>> 8) & 0xff]);
}

function bytesEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) {
    return false;
  }
  for (let i = 0; i < a.length; ++i) {
    if (a[i] !== b[i]) {
      return false;
    }
  }
  return true;
}

function main(): number {
  const vectorsPath = process.argv[2];
  if (!vectorsPath) {
    console.error("usage: driver <vectors-file>");
    return 2;
  }
  const text = fs.readFileSync(vectorsPath, "utf-8");
  let processed = 0;
  let skipped = 0;

  for (const rawLine of text.split("\n")) {
    const line = rawLine.split("#", 1)[0].trim();
    if (line.length === 0) {
      continue;
    }
    const f = line.split(/\s+/);
    const op = f[0];
    const fail = (detail: string): never => {
      console.error(`MISMATCH ${op}: ${detail}`);
      process.exit(1);
    };

    if (op === "f16pack") {
      const buf = new Uint8Array(2);
      writeFloat(buf, 0, 16, f32FromBits(f[1]));
      const got = (buf[0] | (buf[1] << 8)) >>> 0;
      const want = parseInt(f[2], 16) >>> 0;
      if (got !== want) {
        fail(`in=${f[1]} want=${want.toString(16)} got=${got.toString(16)}`);
      }
    } else if (op === "f16unpack") {
      const want = parseInt(f[2], 16) >>> 0;
      const isNanResult = (want & 0x7f800000) === 0x7f800000 && (want & 0x007fffff) !== 0;
      if (isNanResult) {
        console.log(`SKIP f16unpack in=${f[1]}: JS number canonicalizes NaN payloads`);
        skipped++;
        continue;
      }
      const got = f32ToBits(readFloat(u16LeBytes(parseInt(f[1], 16)), 0, 16));
      if (got !== want) {
        fail(`in=${f[1]} want=${want.toString(16)} got=${got.toString(16)}`);
      }
    } else if (op === "geti" || op === "getu") {
      const lenBits = parseInt(f[2], 10);
      const offBits = parseInt(f[3], 10);
      const buf = hexBytes(f[4]);
      const want = BigInt("0x" + f[5]);
      const raw = op === "geti" ? readSignedBigInt(buf, offBits, lenBits) : readUnsignedBigInt(buf, offBits, lenBits);
      const got = BigInt.asUintN(64, raw);
      if (got !== want) {
        fail(`len=${lenBits} off=${offBits} buf=${f[4]} want=${want.toString(16)} got=${got.toString(16)}`);
      }
    } else if (op === "copybits") {
      const dstOff = parseInt(f[1], 10);
      const lenBits = parseInt(f[2], 10);
      const srcOff = parseInt(f[3], 10);
      const src = hexBytes(f[4]);
      const dst = hexBytes(f[5]);
      const want = hexBytes(f[6]);
      copyBits(dst, dstOff, src, srcOff, lenBits);
      if (!bytesEqual(dst, want)) {
        fail(`dstoff=${dstOff} len=${lenBits} srcoff=${srcOff} got=${toHex(dst)}`);
      }
    } else {
      console.error(`UNRECOGNIZED vector: ${line}`);
      return 1;
    }
    processed++;
  }

  console.log(`PROCESSED ${processed}`);
  console.log(`SKIPPED ${skipped}`);
  console.log("TS primitive equivalence PASS");
  return 0;
}

process.exit(main());
