//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading of a container holding a view, in TypeScript: the view is a subarray of the buffer. Compiled under noUnusedLocals.
//
//===----------------------------------------------------------------------===//

import * as frameModule from "./fixtures_views/vendor/frame_1_0";
import * as poseModule from "./fixtures_aliasable/vendor/pose_1_0";
import * as vec3Module from "./fixtures_aliasable/vendor/vec3_1_0";
let failures = 0;
const check = (what: string, ok: boolean): void => { console.log(`  ${what.padEnd(44)} ${ok ? "ok" : "FAILED"}`); if (!ok) failures += 1; };
const wire = new Uint8Array(41);
const view = new DataView(wire.buffer);
view.setUint32(0, 0x11223344, true);
[1.5, -2.25, 3.0, 4.0, 5.5, 6.75].forEach((v, i) => view.setFloat32(4 + i * 4, v, true));
[7.0, 8.0, 9.0].forEach((v, i) => view.setFloat32(28 + i * 4, v, true));
wire[40] = 0x5a;
const decoded = frameModule.deserializeFrame(wire);
const frame = decoded.value;
check("deserialise accepted", decoded.consumed === 41);
check("sequence decoded", frame.sequence === 0x11223344);
check("view points into the buffer", frame.pose.buffer === wire.buffer && frame.pose.byteOffset === 4 && frame.pose.length === 24);
check("orientation.y read through the view", vec3Module.getVec3Y(poseModule.getPoseOrientation(frame.pose)) === 5.5);
check("velocity decoded, status after the view", frame.velocity.y === 8.0 && frame.status === 0x5a);
const out = frameModule.serializeFrame(frame);
check("serialise reproduces the wire", out.length === 41 && out.every((b, i) => b === wire[i]));
const short = frameModule.deserializeFrame(wire.subarray(0, 16)).value;
check("short view holds what was there", short.pose.length === 12 && short.pose.byteOffset === 4);
const o = poseModule.getPoseOrientation(short.pose);
check("missing orientation reads as zero", o.length === 0 && vec3Module.getVec3Y(o) === 0 && short.status === 0);
const shortOut = frameModule.serializeFrame(short);
check("short view serialises zero-filled", shortOut.subarray(4, 16).every((b, i) => b === wire[4 + i]) && shortOut.subarray(16, 28).every((b) => b === 0));
const fresh = frameModule.makeFrame();
check("fresh object holds an empty view", fresh.pose.length === 0);
const freshOut = frameModule.serializeFrame(fresh);
check("empty view serialises as zeros", freshOut.subarray(4, 28).every((b) => b === 0));
console.log(`container-views TypeScript: ${failures === 0 ? "ok" : "FAILED"}`);
if (failures !== 0) { throw new Error("container-views TypeScript probe failed"); }
