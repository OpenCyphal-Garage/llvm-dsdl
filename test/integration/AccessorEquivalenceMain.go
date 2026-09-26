package main

import (
	"fmt"
	"math"
	"os"

	angle "uavcan_dsdl_generated/uavcan/si/unit/angle"
	sample "uavcan_dsdl_generated/uavcan/si/sample/temperature"
	uavtime "uavcan_dsdl_generated/uavcan/time"
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
func checkField[V any](size int, get func([]byte) V, set func([]byte, V) error, field func([]byte) (V, bool), exact bool) bool {
	wire := make([]byte, size)
	fill(wire)
	ok := true
	full, rc := field(wire)
	ok = ok && rc && bitsOf(get(wire)) == bitsOf(full)
	short, rc := field(wire[:size/2])
	ok = ok && rc && bitsOf(get(wire[:size/2])) == bitsOf(short)
	out := make([]byte, size)
	v := get(wire)
	ok = ok && set(out, v) == nil
	back, rc := field(out)
	ok = ok && rc && bitsOf(get(out)) == bitsOf(back)
	ok = ok && (!exact || bitsOf(v) == bitsOf(back))
	ok = ok && set(out[:0], v) != nil
	return ok
}

// An element of a fixed array, through the index the accessor takes; one past the capacity reads
// as zero and cannot be set.
func checkElement[V any](size int, capacity int, get func([]byte, int) V, set func([]byte, int, V) error, element func([]byte, int) (V, bool)) bool {
	ok := true
	for i := 0; i < capacity; i++ {
		wire := make([]byte, size)
		fill(wire)
		full, rc := element(wire, i)
		ok = ok && rc && bitsOf(get(wire, i)) == bitsOf(full)
		ok = ok && bitsOf(get(wire, capacity)) == 0
		out := make([]byte, size)
		v := get(wire, i)
		ok = ok && set(out, i, v) == nil
		back, rc := element(out, i)
		ok = ok && rc && bitsOf(get(out, i)) == bitsOf(back)
		ok = ok && set(out, capacity, v) != nil
	}
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
			_, err := o.Deserialize(b)
			return o.Major, err == nil
		}, true) &&
			checkField(2, node.VersionGetMinor, node.VersionSetMinor, func(b []byte) (uint8, bool) {
				var o node.Version
				_, err := o.Deserialize(b)
				return o.Minor, err == nil
			}, true))
	report("uavcan.primitive.scalar.Integer16",
		checkField(2, scalar.Integer16GetValue, scalar.Integer16SetValue, func(b []byte) (int16, bool) {
			var o scalar.Integer16
			_, err := o.Deserialize(b)
			return o.Value, err == nil
		}, true))
	report("uavcan.primitive.scalar.Natural64",
		checkField(8, scalar.Natural64GetValue, scalar.Natural64SetValue, func(b []byte) (uint64, bool) {
			var o scalar.Natural64
			_, err := o.Deserialize(b)
			return o.Value, err == nil
		}, true))
	report("uavcan.primitive.scalar.Real16",
		checkField(2, scalar.Real16GetValue, scalar.Real16SetValue, func(b []byte) (float32, bool) {
			var o scalar.Real16
			_, err := o.Deserialize(b)
			return o.Value, err == nil
		}, false))
	report("uavcan.primitive.scalar.Real64",
		checkField(8, scalar.Real64GetValue, scalar.Real64SetValue, func(b []byte) (float64, bool) {
			var o scalar.Real64
			_, err := o.Deserialize(b)
			return o.Value, err == nil
		}, false))
	report("uavcan.si.unit.temperature.Scalar",
		checkField(4, temperature.ScalarGetKelvin, temperature.ScalarSetKelvin, func(b []byte) (float32, bool) {
			var o temperature.Scalar
			_, err := o.Deserialize(b)
			return o.Kelvin, err == nil
		}, false))
	report("uavcan.file.Error",
		checkField(2, file.ErrorGetValue, file.ErrorSetValue, func(b []byte) (uint16, bool) {
			var o file.Error
			_, err := o.Deserialize(b)
			return o.Value, err == nil
		}, true))
	report("uavcan.si.unit.angle.Quaternion",
		checkElement(16, 4, angle.QuaternionGetWxyz, angle.QuaternionSetWxyz, func(b []byte, i int) (float32, bool) {
			var o angle.Quaternion
			_, err := o.Deserialize(b)
			return o.Wxyz[i], err == nil
		}))
	// A nested composite, through the buffer its getter answers: the nested type's own getter on it
	// agrees with deserialise on the full buffer and on one cut inside the nested field.
	{
		wire := make([]byte, 11)
		fill(wire)
		var obj sample.Scalar
		_, err := obj.Deserialize(wire)
		same := err == nil && uavtime.SynchronizedTimestampGetMicrosecond(sample.ScalarGetTimestamp(wire)) == obj.Timestamp.Microsecond
		var short sample.Scalar
		_, err = short.Deserialize(wire[:3])
		stamp := sample.ScalarGetTimestamp(wire[:3])
		same = same && err == nil && len(stamp) == 3 && uavtime.SynchronizedTimestampGetMicrosecond(stamp) == short.Timestamp.Microsecond
		same = same && checkField(11, sample.ScalarGetKelvin, sample.ScalarSetKelvin, func(b []byte) (float32, bool) {
			var o sample.Scalar
			_, err := o.Deserialize(b)
			return o.Kelvin, err == nil
		}, false)
		report("uavcan.si.sample.temperature.Scalar", same)
	}
	if failures != 0 {
		os.Exit(1)
	}
}
