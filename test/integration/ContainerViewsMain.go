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
	rc, n := frame.Deserialize(wire)
	check("deserialise accepted", rc == 0 && n == 41)
	check("sequence decoded", frame.Sequence == 0x11223344)
	check("view points into the buffer", len(frame.Pose) == 24 && &frame.Pose[0] == &wire[4])
	check("orientation.y read through the view", aliasable.Vec3GetY(aliasable.PoseGetOrientation(frame.Pose)) == 5.5)
	check("velocity decoded, status after the view", frame.Velocity.Y == 8.0 && frame.Status == 0x5A)
	out := bytes.Repeat([]byte{0xEE}, 64)
	rc, n = frame.Serialize(out)
	check("serialise reproduces the wire", rc == 0 && n == 41 && bytes.Equal(out[:41], wire))
	var short views.Frame
	rc, _ = short.Deserialize(wire[:16])
	check("short deserialise accepted", rc == 0)
	check("short view holds what was there", len(short.Pose) == 12 && &short.Pose[0] == &wire[4])
	o := aliasable.PoseGetOrientation(short.Pose)
	check("missing orientation reads as zero", len(o) == 0 && aliasable.Vec3GetY(o) == 0 && short.Status == 0)
	out = bytes.Repeat([]byte{0xEE}, 64)
	rc, _ = short.Serialize(out)
	check("short view serialises zero-filled", rc == 0 && bytes.Equal(out[4:16], wire[4:16]) && bytes.Equal(out[16:28], make([]byte, 12)))
	var fresh views.Frame
	check("fresh object holds an empty view", len(fresh.Pose) == 0)
	out = bytes.Repeat([]byte{0xEE}, 64)
	rc, _ = fresh.Serialize(out)
	check("empty view serialises as zeros", rc == 0 && bytes.Equal(out[4:28], make([]byte, 24)))
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
	rc, n = track.Deserialize(trackWire)
	check("track deserialise accepted", rc == 0 && n == 99)
	check("pair elements are views into the buffer", len(track.Pair[0]) == 24 && &track.Pair[0][0] == &trackWire[1] && len(track.Pair[1]) == 24 && &track.Pair[1][0] == &trackWire[25])
	check("trail keeps its count, elements are views", len(track.Trail) == 2 && len(track.Trail[1]) == 24 && &track.Trail[1][0] == &trackWire[74])
	check("orientation.y read through pair[1]", aliasable.Vec3GetY(aliasable.PoseGetOrientation(track.Pair[1])) == 50.0 && track.Kind == 0x07 && track.Status == 0x3C)
	out = bytes.Repeat([]byte{0xEE}, 128)
	rc, n = track.Serialize(out)
	check("track serialise reproduces the wire", rc == 0 && n == 99 && bytes.Equal(out[:99], trackWire))
	var shortTrack views.Track
	rc, _ = shortTrack.Deserialize(trackWire[:37])
	check("short track: pair[1] short, trail empty", rc == 0 && len(shortTrack.Pair[1]) == 12 && &shortTrack.Pair[1][0] == &trackWire[25] && len(shortTrack.Trail) == 0 && shortTrack.Status == 0)
	out = bytes.Repeat([]byte{0xEE}, 128)
	rc, n = shortTrack.Serialize(out)
	check("short element serialises zero-filled", rc == 0 && n == 51 && bytes.Equal(out[25:37], trackWire[25:37]) && bytes.Equal(out[37:51], make([]byte, 14)))
	var freshTrack views.Track
	check("fresh object holds empty element views", len(freshTrack.Pair[0]) == 0 && len(freshTrack.Pair[1]) == 0 && len(freshTrack.Trail) == 0)
	out = bytes.Repeat([]byte{0xEE}, 128)
	rc, n = freshTrack.Serialize(out)
	check("empty element views serialise as zeros", rc == 0 && n == 51 && bytes.Equal(out[1:51], make([]byte, 50)))
	verdict := "ok"
	if failures != 0 {
		verdict = "FAILED"
	}
	fmt.Printf("container-views Go: %s\n", verdict)
	if failures != 0 {
		os.Exit(1)
	}
}
