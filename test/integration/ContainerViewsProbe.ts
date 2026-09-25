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
import * as leadingModule from "./fixtures_views/vendor/leading_1_0";
import * as trackModule from "./fixtures_views/vendor/track_1_0";
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
const trackWire = new Uint8Array(99);
const trackView = new DataView(trackWire.buffer);
trackWire[0] = 0x07;
[[1, 2, 3, 4, 5, 6], [10, 20, 30, 40, 50, 60], [-1, -2, -3, -4, -5, -6], [0.5, 1.5, 2.5, 3.5, 4.5, 5.5]].forEach((pose, p) => pose.forEach((v, i) => trackView.setFloat32([1, 25, 50, 74][p] + i * 4, v, true)));
trackWire[49] = 2;
trackWire[98] = 0x3c;
const trackDecoded = trackModule.deserializeTrack(trackWire);
const track = trackDecoded.value;
check("track deserialise accepted", trackDecoded.consumed === 99);
check("pair elements are views into the buffer", track.pair[0].buffer === trackWire.buffer && track.pair[0].byteOffset === 1 && track.pair[0].length === 24 && track.pair[1].byteOffset === 25 && track.pair[1].length === 24);
check("trail keeps its count, elements are views", track.trail.length === 2 && track.trail[1].byteOffset === 74 && track.trail[1].length === 24);
check("orientation.y read through pair[1]", vec3Module.getVec3Y(poseModule.getPoseOrientation(track.pair[1])) === 50.0 && track.kind === 0x07 && track.status === 0x3c);
const trackOut = trackModule.serializeTrack(track);
check("track serialise reproduces the wire", trackOut.length === 99 && trackOut.every((b, i) => b === trackWire[i]));
const shortTrack = trackModule.deserializeTrack(trackWire.subarray(0, 37)).value;
check("short track: pair[1] short, trail empty", shortTrack.pair[1].byteOffset === 25 && shortTrack.pair[1].length === 12 && shortTrack.trail.length === 0 && shortTrack.status === 0);
const shortTrackOut = trackModule.serializeTrack(shortTrack);
check("short element serialises zero-filled", shortTrackOut.length === 51 && shortTrackOut.subarray(25, 37).every((b, i) => b === trackWire[25 + i]) && shortTrackOut.subarray(37, 51).every((b) => b === 0));
const freshTrack = trackModule.makeTrack();
check("fresh object holds empty element views", freshTrack.pair.length === 2 && freshTrack.pair.every((p) => p.length === 0) && freshTrack.trail.length === 0);
const freshTrackOut = trackModule.serializeTrack(freshTrack);
check("empty element views serialise as zeros", freshTrackOut.length === 51 && freshTrackOut.subarray(1, 51).every((b) => b === 0));
const leadWire = new Uint8Array(25);
const leadView = new DataView(leadWire.buffer);
[0.25, -0.5, 0.75, -1.0, 1.25, -1.5].forEach((v, i) => leadView.setFloat32(i * 4, v, true));
leadWire[24] = 0xa5;
const leadDecoded = leadingModule.deserializeLeading(leadWire);
const lead = leadDecoded.value;
check("leading deserialise accepted", leadDecoded.consumed === 25);
check("leading view is the buffer itself", lead.pose.buffer === leadWire.buffer && lead.pose.byteOffset === 0 && lead.pose.length === 24);
check("orientation.y read through the leading view", vec3Module.getVec3Y(poseModule.getPoseOrientation(lead.pose)) === 1.25 && lead.status === 0xa5);
const leadOut = new Uint8Array(64).fill(0xee);
check("leading serialise reproduces the wire", leadingModule.serializeLeadingInto(lead, leadOut) === 25 && leadOut.subarray(0, 25).every((b, i) => b === leadWire[i]));
const shortLead = leadingModule.deserializeLeading(leadWire.subarray(0, 12)).value;
check("short leading view holds what was there", shortLead.pose.buffer === leadWire.buffer && shortLead.pose.byteOffset === 0 && shortLead.pose.length === 12 && shortLead.status === 0);
const shortLeadOut = new Uint8Array(64).fill(0xee);
check("short leading view serialises zero-filled", leadingModule.serializeLeadingInto(shortLead, shortLeadOut) === 25 && shortLeadOut.subarray(0, 12).every((b, i) => b === leadWire[i]) && shortLeadOut.subarray(12, 25).every((b) => b === 0));
const emptyDecoded = leadingModule.deserializeLeading(new Uint8Array(0));
const emptyOrientation = poseModule.getPoseOrientation(emptyDecoded.value.pose);
check("empty buffer leaves an empty leading view", emptyDecoded.consumed === 0 && emptyDecoded.value.pose.length === 0 && emptyDecoded.value.status === 0 && emptyOrientation.length === 0 && vec3Module.getVec3Y(emptyOrientation) === 0);
const freshLead = leadingModule.makeLeading();
check("fresh object holds an empty leading view", freshLead.pose.length === 0);
const freshLeadOut = new Uint8Array(64).fill(0xee);
check("empty leading view serialises as zeros", leadingModule.serializeLeadingInto(freshLead, freshLeadOut) === 25 && freshLeadOut.subarray(0, 25).every((b) => b === 0));
console.log(`container-views TypeScript: ${failures === 0 ? "ok" : "FAILED"}`);
if (failures !== 0) { throw new Error("container-views TypeScript probe failed"); }
