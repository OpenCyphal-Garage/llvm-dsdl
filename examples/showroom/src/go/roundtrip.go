// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
// Shared by every Go recipe in the matrix. See src/README.md for what these programs are for and
// what they deliberately do not test.

package main

// The generation step, where Go expects to find it: beside the code that consumes the result.
// `go generate ./...` runs this, expanding $DSDLC from the environment. Paths are relative to this
// file's directory, which is where go generate runs the command.
//
// go generate does not type-check the package, so this works on a clean checkout where
// ../../generated does not exist yet and the import below cannot resolve.
//
//go:generate $DSDLC --target-language go --versioned-type-names ../../dsdl/lanyard --go-module example.com/lanyard --outdir ../../generated

import (
	"fmt"
	"os"
	"reflect"

	"example.com/lanyard/lanyard/health"
)

var subsystems = []string{"gnss", "esc.3", "imu.0"}

func fail(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "FAIL: "+format+"\n", args...)
	os.Exit(1)
}

func main() {
	// Deliberately not the zero value: an integration that serialised nothing and deserialised
	// nothing would round-trip a zero struct perfectly and prove nothing at all.
	var original health.SystemHealth_1_0
	original.Timestamp.Microsecond = 1234567890123
	original.AggregateHealth.Value = 2 // CAUTION, on the standard four-level scale

	for i, name := range subsystems {
		var report health.SubsystemReport_1_0
		report.Health.Value = uint8(i % 4)
		report.Severity.Value = uint8(i % 8)
		report.FaultCode = uint16(0x1000 + i)
		report.Name = []byte(name)
		original.Subsystem = append(original.Subsystem, report)
	}

	buffer := make([]byte, health.SYSTEM_HEALTH_1_0_SERIALIZATION_BUFFER_SIZE_BYTES)
	rc, written := original.Serialize(buffer)
	if rc != 0 {
		fail("serialize returned %d", rc)
	}

	var restored health.SystemHealth_1_0
	rc, _ = restored.Deserialize(buffer[:written])
	if rc != 0 {
		fail("deserialize returned %d", rc)
	}

	// Generated Go types are plain structs over plain slices, so this is a complete comparison and
	// needs no field-by-field spelling out.
	if !reflect.DeepEqual(restored, original) {
		fail("round-trip changed the value\n  sent:     %+v\n  received: %+v", original, restored)
	}

	fmt.Printf("round-trip OK: %d subsystems, %d bytes on the wire\n",
		len(restored.Subsystem), written)
}
