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
const ok = choice.getChoice_tag(wire) === 1 && choice.getChoiceReal(wire) === 5.5 && choice.getChoiceQuad(wire).length === 4 && choice.setChoice_tag(wire, 0) === 0;
console.log(`union-accessors TypeScript: ${ok ? "ok" : "FAILED"}`);
if (!ok) { throw new Error("union-accessors TypeScript probe failed"); }
