//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

// Array-length prefixes decoded by the generated Go: above the capacity, beyond the width of
// int, and at the capacity. One line per case and a summary line the lane parses. The lane runs
// the binary on the host and builds it for linux/386, where int is 32 bits wide.
package main

import (
	"encoding/binary"
	"fmt"
	"math"
	"os"

	dsdlruntime "prefixguard_generated/dsdlruntime"
	prefixguard "prefixguard_generated/prefixguard"
)

const badArrayLength = -dsdlruntime.DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH

var passed, skipped, failed int

func outcome(verdict string, name string, prefix uint64, detail string) {
	switch verdict {
	case "PASS":
		passed++
	case "SKIP":
		skipped++
	default:
		failed++
	}
	switch {
	case detail == "":
		fmt.Printf("%s %s prefix=%d\n", verdict, name, prefix)
	case verdict == "FAIL":
		fmt.Printf("%s %s prefix=%d: %s\n", verdict, name, prefix, detail)
	default:
		fmt.Printf("%s %s prefix=%d %s\n", verdict, name, prefix, detail)
	}
}

// withPrefix returns a buffer opening with a little-endian prefix of prefixBytes bytes followed
// by payloadBytes zero bytes.
func withPrefix(prefix uint64, prefixBytes int, payloadBytes int) []byte {
	buffer := make([]byte, prefixBytes+payloadBytes)
	var wide [8]byte
	binary.LittleEndian.PutUint64(wide[:], prefix)
	copy(buffer, wide[:prefixBytes])
	return buffer
}

// expectRejected records the verdict for a decode that has to fail with a bad array length and
// leave the array empty.
func expectRejected(name string, prefix uint64, rc int8, lenAfter int) {
	if rc != badArrayLength {
		outcome("FAIL", name, prefix, fmt.Sprintf("rc = %d, want %d", rc, badArrayLength))
		return
	}
	if lenAfter != 0 {
		outcome("FAIL", name, prefix, fmt.Sprintf("array holds %d elements after rejection", lenAfter))
		return
	}
	outcome("PASS", name, prefix, "")
}

func prefix32RejectsAboveCapacity(prefix uint32) {
	var obj prefixguard.Prefix32@V1_0@
	rc, _ := obj.Deserialize(withPrefix(uint64(prefix), 4, 0))
	expectRejected("prefix32_rejects_above_capacity", uint64(prefix), rc, len(obj.Payload))
}

func prefix32AcceptsCapacity() {
	const capacity = 65536
	var obj prefixguard.Prefix32@V1_0@
	rc, consumed := obj.Deserialize(withPrefix(capacity, 4, capacity))
	switch {
	case rc != dsdlruntime.DSDL_RUNTIME_SUCCESS:
		outcome("FAIL", "prefix32_accepts_capacity", capacity, fmt.Sprintf("rc = %d, want success", rc))
	case consumed != prefixguard.Prefix32@V1_0@SerializationBufferSizeBytes:
		outcome("FAIL", "prefix32_accepts_capacity", capacity, fmt.Sprintf("consumed %d bytes, want %d",
			consumed, prefixguard.Prefix32@V1_0@SerializationBufferSizeBytes))
	case len(obj.Payload) != capacity:
		outcome("FAIL", "prefix32_accepts_capacity", capacity, fmt.Sprintf("payload holds %d elements, want %d",
			len(obj.Payload), capacity))
	default:
		outcome("PASS", "prefix32_accepts_capacity", capacity, "")
	}
}

func prefix64RejectsAboveCapacity(prefix uint64) {
	var obj prefixguard.Prefix64@V1_0@
	rc, _ := obj.Deserialize(withPrefix(prefix, 8, 0))
	expectRejected("prefix64_rejects_above_capacity", prefix, rc, len(obj.Flags))
}

// prefix64RejectsBeyondIndex decodes a length within the type's capacity that int cannot hold,
// which is rejected as a bad array length. A 64-bit int holds every length this type allows, so
// the case is skipped there.
func prefix64RejectsBeyondIndex(prefix uint64) {
	if uint64(math.MaxInt) >= 1<<33 {
		outcome("SKIP", "prefix64_rejects_beyond_index", prefix, "int holds every length Prefix64 allows")
		return
	}
	var obj prefixguard.Prefix64@V1_0@
	rc, _ := obj.Deserialize(withPrefix(prefix, 8, 0))
	expectRejected("prefix64_rejects_beyond_index", prefix, rc, len(obj.Flags))
}

func prefix64AcceptsSmallLength() {
	var obj prefixguard.Prefix64@V1_0@
	buffer := withPrefix(3, 8, 1)
	buffer[8] = 0x05
	rc, consumed := obj.Deserialize(buffer)
	want := []bool{true, false, true}
	switch {
	case rc != dsdlruntime.DSDL_RUNTIME_SUCCESS:
		outcome("FAIL", "prefix64_accepts_small_length", 3, fmt.Sprintf("rc = %d, want success", rc))
	case consumed != len(buffer):
		outcome("FAIL", "prefix64_accepts_small_length", 3, fmt.Sprintf("consumed %d bytes, want %d", consumed, len(buffer)))
	case len(obj.Flags) != len(want) || obj.Flags[0] != want[0] || obj.Flags[1] != want[1] || obj.Flags[2] != want[2]:
		outcome("FAIL", "prefix64_accepts_small_length", 3, fmt.Sprintf("flags = %v, want %v", obj.Flags, want))
	default:
		outcome("PASS", "prefix64_accepts_small_length", 3, "")
	}
}

func main() {
	for _, prefix := range []uint32{65537, 1 << 31, math.MaxUint32} {
		prefix32RejectsAboveCapacity(prefix)
	}
	prefix32AcceptsCapacity()
	for _, prefix := range []uint64{1<<33 + 1, 1<<33 + 3, 1 << 63, math.MaxUint64} {
		prefix64RejectsAboveCapacity(prefix)
	}
	for _, prefix := range []uint64{1 << 32, 1<<32 + 3, 1 << 33} {
		prefix64RejectsBeyondIndex(prefix)
	}
	prefix64AcceptsSmallLength()

	status := "PASS"
	if failed != 0 {
		status = "FAIL"
	}
	fmt.Printf("%s go-array-length-prefix-guard cases=%d passed=%d skipped=%d failed=%d\n",
		status, passed+skipped+failed, passed, skipped, failed)
	if failed != 0 {
		os.Exit(1)
	}
}
