package main

import (
	"fmt"
	"math"
	"os"

	file "uavcan_dsdl_generated/uavcan/file"
	node "uavcan_dsdl_generated/uavcan/node"
	scalar "uavcan_dsdl_generated/uavcan/primitive/scalar"
	temperature "uavcan_dsdl_generated/uavcan/si/unit/temperature"
)

// An accessor against the body it stands in for: a getter answers what deserialise puts in the
// field, on a full buffer and on a short one, and a setter writes what deserialise reads back.
// Values are compared as bits, so a NaN meets itself; an integer round trip is also exact.
var rng uint32 = 0x9E3779B9

func fill(buffer []byte) {
	for i := range buffer {
		rng ^= rng << 13
		rng ^= rng >> 17
		rng ^= rng << 5
		buffer[i] = byte(rng)
	}
}

func bitsOf(v any) uint64 {
	switch x := v.(type) {
	case uint8:
		return uint64(x)
	case uint16:
		return uint64(x)
	case int16:
		return uint64(uint16(x))
	case uint64:
		return x
	case float32:
		return uint64(math.Float32bits(x))
	case float64:
		return math.Float64bits(x)
	}
	panic("a kind the driver does not compare")
}

// field deserialises the buffer into a fresh object and answers the member and whether it succeeded.
func checkField[V any](size int, get func([]byte) V, set func([]byte, V) int8, field func([]byte) (V, bool), exact bool) bool {
	wire := make([]byte, size)
	fill(wire)
	ok := true
	full, rc := field(wire)
	ok = ok && rc && bitsOf(get(wire)) == bitsOf(full)
	short, rc := field(wire[:size/2])
	ok = ok && rc && bitsOf(get(wire[:size/2])) == bitsOf(short)
	out := make([]byte, size)
	v := get(wire)
	ok = ok && set(out, v) == 0
	back, rc := field(out)
	ok = ok && rc && bitsOf(get(out)) == bitsOf(back)
	ok = ok && (!exact || bitsOf(v) == bitsOf(back))
	ok = ok && set(out[:0], v) != 0
	return ok
}

var failures = 0

func report(name string, same bool) {
	verdict := "same"
	if !same {
		verdict = "DIFFER"
		failures++
	}
	fmt.Printf("%-40s %s\n", name, verdict)
}

func main() {
	report("uavcan.node.Version",
		checkField(2, node.VersionGetMajor, node.VersionSetMajor, func(b []byte) (uint8, bool) {
			var o node.Version
			rc, _ := o.Deserialize(b)
			return o.Major, rc == 0
		}, true) &&
			checkField(2, node.VersionGetMinor, node.VersionSetMinor, func(b []byte) (uint8, bool) {
				var o node.Version
				rc, _ := o.Deserialize(b)
				return o.Minor, rc == 0
			}, true))
	report("uavcan.primitive.scalar.Integer16",
		checkField(2, scalar.Integer16GetValue, scalar.Integer16SetValue, func(b []byte) (int16, bool) {
			var o scalar.Integer16
			rc, _ := o.Deserialize(b)
			return o.Value, rc == 0
		}, true))
	report("uavcan.primitive.scalar.Natural64",
		checkField(8, scalar.Natural64GetValue, scalar.Natural64SetValue, func(b []byte) (uint64, bool) {
			var o scalar.Natural64
			rc, _ := o.Deserialize(b)
			return o.Value, rc == 0
		}, true))
	report("uavcan.primitive.scalar.Real16",
		checkField(2, scalar.Real16GetValue, scalar.Real16SetValue, func(b []byte) (float32, bool) {
			var o scalar.Real16
			rc, _ := o.Deserialize(b)
			return o.Value, rc == 0
		}, false))
	report("uavcan.primitive.scalar.Real64",
		checkField(8, scalar.Real64GetValue, scalar.Real64SetValue, func(b []byte) (float64, bool) {
			var o scalar.Real64
			rc, _ := o.Deserialize(b)
			return o.Value, rc == 0
		}, false))
	report("uavcan.si.unit.temperature.Scalar",
		checkField(4, temperature.ScalarGetKelvin, temperature.ScalarSetKelvin, func(b []byte) (float32, bool) {
			var o temperature.Scalar
			rc, _ := o.Deserialize(b)
			return o.Kelvin, rc == 0
		}, false))
	report("uavcan.file.Error",
		checkField(2, file.ErrorGetValue, file.ErrorSetValue, func(b []byte) (uint16, bool) {
			var o file.Error
			rc, _ := o.Deserialize(b)
			return o.Value, rc == 0
		}, true))
	if failures != 0 {
		os.Exit(1)
	}
}
