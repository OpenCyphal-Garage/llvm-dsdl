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
	verdict := "ok"
	if failures != 0 {
		verdict = "FAILED"
	}
	fmt.Printf("container-views Go: %s\n", verdict)
	if failures != 0 {
		os.Exit(1)
	}
}
