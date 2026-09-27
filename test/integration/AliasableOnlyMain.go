//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading, through the Go package-level accessors.
//
//===----------------------------------------------------------------------===//

package main

import (
	"encoding/binary"
	"fmt"
	"math"
	"os"

	vendor "aliasable_only_generated/fixtures_aliasable/vendor"
)

func main() {
	buffer := make([]byte, 24)
	for i, v := range []float32{1.5, -2.25, 3.0, 4.0, 5.5, 6.75} {
		binary.LittleEndian.PutUint32(buffer[i*4:], math.Float32bits(v))
	}

	orientation := vendor.PoseGetOrientation(buffer)
	y := vendor.Vec3GetY(orientation)
	setResult := vendor.Vec3SetZ(buffer[:12], 9.5)
	z := vendor.Vec3GetZ(buffer)
	shortRead := vendor.Vec3GetZ(buffer[:4])
	x := vendor.Vec3GetX(vendor.PoseGetPosition(buffer))

	ok := len(orientation) == 12 && y == 5.5 && setResult == nil && z == 9.5 && shortRead == 0 && x == 1.5
	verdict := "ok"
	if !ok {
		verdict = "FAILED"
	}
	fmt.Printf("aliasable-only Go: %s (orientation %d bytes, y %g, set %v, z %g, short %g, x %g)\n",
		verdict, len(orientation), y, setResult, z, shortRead, x)
	if !ok {
		os.Exit(1)
	}
}
