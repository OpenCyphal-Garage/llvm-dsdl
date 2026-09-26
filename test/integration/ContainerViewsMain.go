//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading of a container holding a view, in Go: the view is a slice of the buffer.
//
//===----------------------------------------------------------------------===//

package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"os"

	aliasable "llvmdsdl_generated/fixtures_aliasable/vendor"
	views "llvmdsdl_generated/fixtures_views/vendor"
)

var failures = 0

func check(what string, ok bool) {
	v := "ok"
	if !ok {
		v = "FAILED"
		failures++
	}
	fmt.Printf("  %-44s %s\n", what, v)
}

func main() {
	wire := make([]byte, 41)
	binary.LittleEndian.PutUint32(wire, 0x11223344)
	for i, v := range []float32{1.5, -2.25, 3.0, 4.0, 5.5, 6.75} {
		binary.LittleEndian.PutUint32(wire[4+i*4:], math.Float32bits(v))
	}
	for i, v := range []float32{7.0, 8.0, 9.0} {
		binary.LittleEndian.PutUint32(wire[28+i*4:], math.Float32bits(v))
	}
	wire[40] = 0x5A
	var frame views.Frame
	n, err := frame.Deserialize(wire)
	check("deserialise accepted", err == nil && n == 41)
	check("sequence decoded", frame.Sequence == 0x11223344)
	check("view points into the buffer", len(frame.Pose) == 24 && &frame.Pose[0] == &wire[4])
	check("orientation.y read through the view", aliasable.Vec3GetY(aliasable.PoseGetOrientation(frame.Pose)) == 5.5)
	check("velocity decoded, status after the view", frame.Velocity.Y == 8.0 && frame.Status == 0x5A)
	out := bytes.Repeat([]byte{0xEE}, 64)
	n, err = frame.Serialize(out)
	check("serialise reproduces the wire", err == nil && n == 41 && bytes.Equal(out[:41], wire))
	var short views.Frame
	_, err = short.Deserialize(wire[:16])
	check("short deserialise accepted", err == nil)
	check("short view holds what was there", len(short.Pose) == 12 && &short.Pose[0] == &wire[4])
	o := aliasable.PoseGetOrientation(short.Pose)
	check("missing orientation reads as zero", len(o) == 0 && aliasable.Vec3GetY(o) == 0 && short.Status == 0)
	out = bytes.Repeat([]byte{0xEE}, 64)
	_, err = short.Serialize(out)
	check("short view serialises zero-filled", err == nil && bytes.Equal(out[4:16], wire[4:16]) && bytes.Equal(out[16:28], make([]byte, 12)))
	var fresh views.Frame
	check("fresh object holds an empty view", len(fresh.Pose) == 0)
	out = bytes.Repeat([]byte{0xEE}, 64)
	_, err = fresh.Serialize(out)
	check("empty view serialises as zeros", err == nil && bytes.Equal(out[4:28], make([]byte, 24)))
	trackWire := make([]byte, 99)
	trackWire[0] = 0x07
	poses := [][]float32{{1, 2, 3, 4, 5, 6}, {10, 20, 30, 40, 50, 60}, {-1, -2, -3, -4, -5, -6}, {0.5, 1.5, 2.5, 3.5, 4.5, 5.5}}
	for p, at := range []int{1, 25, 50, 74} {
		for i, v := range poses[p] {
			binary.LittleEndian.PutUint32(trackWire[at+i*4:], math.Float32bits(v))
		}
	}
	trackWire[49] = 2
	trackWire[98] = 0x3C
	var track views.Track
	n, err = track.Deserialize(trackWire)
	check("track deserialise accepted", err == nil && n == 99)
	check("pair elements are views into the buffer", len(track.Pair[0]) == 24 && &track.Pair[0][0] == &trackWire[1] && len(track.Pair[1]) == 24 && &track.Pair[1][0] == &trackWire[25])
	check("trail keeps its count, elements are views", len(track.Trail) == 2 && len(track.Trail[1]) == 24 && &track.Trail[1][0] == &trackWire[74])
	check("orientation.y read through pair[1]", aliasable.Vec3GetY(aliasable.PoseGetOrientation(track.Pair[1])) == 50.0 && track.Kind == 0x07 && track.Status == 0x3C)
	out = bytes.Repeat([]byte{0xEE}, 128)
	n, err = track.Serialize(out)
	check("track serialise reproduces the wire", err == nil && n == 99 && bytes.Equal(out[:99], trackWire))
	var shortTrack views.Track
	_, err = shortTrack.Deserialize(trackWire[:37])
	check("short track: pair[1] short, trail empty", err == nil && len(shortTrack.Pair[1]) == 12 && &shortTrack.Pair[1][0] == &trackWire[25] && len(shortTrack.Trail) == 0 && shortTrack.Status == 0)
	out = bytes.Repeat([]byte{0xEE}, 128)
	n, err = shortTrack.Serialize(out)
	check("short element serialises zero-filled", err == nil && n == 51 && bytes.Equal(out[25:37], trackWire[25:37]) && bytes.Equal(out[37:51], make([]byte, 14)))
	var freshTrack views.Track
	check("fresh object holds empty element views", len(freshTrack.Pair[0]) == 0 && len(freshTrack.Pair[1]) == 0 && len(freshTrack.Trail) == 0)
	out = bytes.Repeat([]byte{0xEE}, 128)
	n, err = freshTrack.Serialize(out)
	check("empty element views serialise as zeros", err == nil && n == 51 && bytes.Equal(out[1:51], make([]byte, 50)))
	leadWire := make([]byte, 25)
	for i, v := range []float32{0.25, -0.5, 0.75, -1.0, 1.25, -1.5} {
		binary.LittleEndian.PutUint32(leadWire[i*4:], math.Float32bits(v))
	}
	leadWire[24] = 0xA5
	var lead views.Leading
	n, err = lead.Deserialize(leadWire)
	check("leading deserialise accepted", err == nil && n == 25)
	check("leading view is the buffer itself", len(lead.Pose) == 24 && &lead.Pose[0] == &leadWire[0])
	check("orientation.y read through the leading view", aliasable.Vec3GetY(aliasable.PoseGetOrientation(lead.Pose)) == 1.25 && lead.Status == 0xA5)
	out = bytes.Repeat([]byte{0xEE}, 64)
	n, err = lead.Serialize(out)
	check("leading serialise reproduces the wire", err == nil && n == 25 && bytes.Equal(out[:25], leadWire))
	var shortLead views.Leading
	_, err = shortLead.Deserialize(leadWire[:12])
	check("short leading view holds what was there", err == nil && len(shortLead.Pose) == 12 && &shortLead.Pose[0] == &leadWire[0] && shortLead.Status == 0)
	out = bytes.Repeat([]byte{0xEE}, 64)
	n, err = shortLead.Serialize(out)
	check("short leading view serialises zero-filled", err == nil && n == 25 && bytes.Equal(out[:12], leadWire[:12]) && bytes.Equal(out[12:25], make([]byte, 13)))
	var emptyLead views.Leading
	n, err = emptyLead.Deserialize(nil)
	o = aliasable.PoseGetOrientation(emptyLead.Pose)
	check("nil buffer leaves an empty leading view", err == nil && n == 0 && len(emptyLead.Pose) == 0 && emptyLead.Status == 0 && len(o) == 0 && aliasable.Vec3GetY(o) == 0)
	var freshLead views.Leading
	check("fresh object holds an empty leading view", len(freshLead.Pose) == 0)
	out = bytes.Repeat([]byte{0xEE}, 64)
	n, err = freshLead.Serialize(out)
	check("empty leading view serialises as zeros", err == nil && n == 25 && bytes.Equal(out[:25], make([]byte, 25)))
	verdict := "ok"
	if failures != 0 {
		verdict = "FAILED"
	}
	fmt.Printf("container-views Go: %s\n", verdict)
	if failures != 0 {
		os.Exit(1)
	}
}
