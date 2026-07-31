// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
// Shared by every Go cell in the matrix. See src/README.md for what these programs are for and what
// they deliberately do not test.

package main

// The generation step, where Go expects to find it: beside the code that consumes the result.
// `go generate ./...` runs this, expanding $DSDLC from the environment. Paths are relative to this
// file's directory, which is where go generate runs the command.
//
// go generate does not type-check the package, so this works on a clean checkout where
// ../../generated does not exist yet and the import below cannot resolve.
//
//go:generate $DSDLC --target-language go ../../dsdl/kitbag --go-module example.com/kitbag --outdir ../../generated

import (
	"fmt"
	"os"
	"reflect"

	"example.com/kitbag/kitbag"
	uavcantime "example.com/kitbag/uavcan/time"
)

func fail(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "FAIL: "+format+"\n", args...)
	os.Exit(1)
}

func main() {
	// Deliberately not the zero value: an integration that serialised nothing and deserialised
	// nothing would round-trip a zero struct perfectly and prove nothing at all.
	original := kitbag.SensorFrame_1_0{
		Timestamp: uavcantime.SynchronizedTimestamp_1_0{Microsecond: 1234567890123},
		Readings:  make([]kitbag.Reading_1_0, 0, 3),
		Source:    []byte("imu.0"),
	}
	for i := uint8(0); i < 3; i++ {
		original.Readings = append(original.Readings, kitbag.Reading_1_0{
			Channel: i + 1,
			// Exactly representable in binary32, so comparing for equality after the round trip is
			// a statement about the wiring rather than about floating-point rounding.
			Value: 0.5 * float32(i+1),
			Mode:  kitbag.Mode_1_0{Value: 2}, // ACTIVE
		})
	}

	buffer := make([]byte, kitbag.SENSOR_FRAME_1_0_SERIALIZATION_BUFFER_SIZE_BYTES)
	rc, written := original.Serialize(buffer)
	if rc != 0 {
		fail("serialize returned %d", rc)
	}

	var restored kitbag.SensorFrame_1_0
	rc, _ = restored.Deserialize(buffer[:written])
	if rc != 0 {
		fail("deserialize returned %d", rc)
	}

	// Generated Go types are plain structs over plain slices, so this is a complete comparison and
	// needs no field-by-field spelling out.
	if !reflect.DeepEqual(restored, original) {
		fail("round-trip changed the value\n  sent:     %+v\n  received: %+v", original, restored)
	}

	fmt.Printf("round-trip OK: %d readings, %d source bytes, %d bytes on the wire\n",
		len(restored.Readings), len(restored.Source), written)
}
