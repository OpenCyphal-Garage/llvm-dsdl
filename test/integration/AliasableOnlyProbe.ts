//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading, through the TypeScript exported accessors. Compiled under
// `noUnusedLocals`, which refuses an import the generated file does not use.
//
//===----------------------------------------------------------------------===//

import * as pose from "./fixtures_aliasable/vendor/pose_1_0";
import * as vec3 from "./fixtures_aliasable/vendor/vec3_1_0";

const buffer = new Uint8Array(24);
const view = new DataView(buffer.buffer);
[1.5, -2.25, 3.0, 4.0, 5.5, 6.75].forEach((v, i) => view.setFloat32(i * 4, v, true));

const orientation = pose.getPoseOrientation(buffer);
const y = vec3.getVec3Y(orientation);
const setResult = vec3.setVec3Z(buffer.subarray(0, 12), 9.5);
const z = vec3.getVec3Z(buffer);
const shortRead = vec3.getVec3Z(buffer.subarray(0, 4));
const x = vec3.getVec3X(pose.getPosePosition(buffer));

const ok = orientation.length === 12 && y === 5.5 && setResult === 0 && z === 9.5 && shortRead === 0 && x === 1.5;
console.log(
  `aliasable-only TypeScript: ${ok ? "ok" : "FAILED"} (orientation ${orientation.length} bytes, y ${y}, set ${setResult}, z ${z}, short ${shortRead}, x ${x})`,
);
if (!ok) {
  throw new Error("aliasable-only TypeScript probe failed");
}
