//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading of an equal-length union, in TypeScript, compiled under noUnusedLocals.
//
//===----------------------------------------------------------------------===//

import * as choice from "./fixtures_union/vendor/choice_1_0";
const wire = new Uint8Array(5); wire[0] = 1; new DataView(wire.buffer).setFloat32(1, 5.5, true);
const read = choice.Choice.getTag(wire) === 1 && choice.Choice.getReal(wire) === 5.5 && choice.Choice.getQuad(wire).length === 4;
choice.Choice.setTag(wire, 0);
const ok = read && choice.Choice.getTag(wire) === 0;
console.log(`union-accessors TypeScript: ${ok ? "ok" : "FAILED"}`);
if (!ok) { throw new Error("union-accessors TypeScript probe failed"); }
