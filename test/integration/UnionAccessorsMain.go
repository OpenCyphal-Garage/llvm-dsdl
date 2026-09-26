//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// The C probe's reading of an equal-length union, in Go.
//
//===----------------------------------------------------------------------===//

package main

import (
	"encoding/binary"
	"fmt"
	"math"
	"os"

	vendor "llvmdsdl_generated/fixtures_union/vendor"
)

func main() {
	wire := []byte{1, 0, 0, 0, 0}
	binary.LittleEndian.PutUint32(wire[1:], math.Float32bits(5.5))
	ok := vendor.ChoiceGetTag(wire) == 1 && vendor.ChoiceGetReal(wire) == 5.5 && len(vendor.ChoiceGetQuad(wire)) == 4 && vendor.ChoiceSetTag(wire, 0) == nil
	v := "ok"
	if !ok {
		v = "FAILED"
	}
	fmt.Printf("union-accessors Go: %s\n", v)
	if !ok {
		os.Exit(1)
	}
}
