//===----------------------------------------------------------------------===//
///
/// @file
/// Go parity driver for signed-narrow C-vs-Go compatibility tests.
///
/// The program runs randomized roundtrips and directed edge-case checks for
/// 3-bit saturating and truncating integer fixtures.
///
//===----------------------------------------------------------------------===//

package main

/*
#include <stddef.h>
#include <stdint.h>

typedef struct CCaseResult {
	int8_t deserialize_rc;
	size_t deserialize_consumed;
	int8_t serialize_rc;
	size_t serialize_size;
} CCaseResult;

int c_int3sat_roundtrip(const uint8_t* input,
                        size_t input_size,
                        uint8_t* output,
                        size_t output_capacity,
                        CCaseResult* result);
int c_int3trunc_roundtrip(const uint8_t* input,
                          size_t input_size,
                          uint8_t* output,
                          size_t output_capacity,
                          CCaseResult* result);

int c_int3sat_directed_serialize(int8_t value,
                                 uint8_t* output,
                                 size_t output_capacity,
                                 CCaseResult* result);
int c_int3trunc_directed_serialize(int8_t value,
                                   uint8_t* output,
                                   size_t output_capacity,
                                   CCaseResult* result);

int c_int3sat_deserialize_value(uint8_t sample, int8_t* out_value, CCaseResult* result);
int c_int3trunc_deserialize_value(uint8_t sample, int8_t* out_value, CCaseResult* result);
*/
import "C"

import (
	"bytes"
	"fmt"
	"os"
	"sort"
	"strconv"
	"strings"
	"unsafe"

	"signed_narrow_generated/vendor"
)

const maxIOBuffer = 64

type cRoundtripFn func(*C.uint8_t, C.size_t, *C.uint8_t, C.size_t, *C.CCaseResult) C.int

func nextRandomU32(state *uint64) uint32 {
	*state ^= *state << 13
	*state ^= *state >> 7
	*state ^= *state << 17
	return uint32(*state & 0xFFFF_FFFF)
}

func fillRandomBytes(dst []byte, state *uint64) {
	for i := range dst {
		dst[i] = byte(nextRandomU32(state) & 0xFF)
	}
}

func formatBytes(data []byte) string {
	var out strings.Builder
	for i, b := range data {
		if i > 0 {
			out.WriteByte(' ')
		}
		fmt.Fprintf(&out, "%02X", b)
	}
	return out.String()
}

func emitCategorySummary(prefix string, counts map[string]int) {
	keys := make([]string, 0, len(counts))
	for k := range counts {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	parts := make([]string, 0, len(keys))
	for _, k := range keys {
		parts = append(parts, fmt.Sprintf("%s=%d", k, counts[k]))
	}
	fmt.Printf("PASS signed-narrow-c-go-parity %s categories %s\n", prefix, strings.Join(parts, " "))
}

func runRandomCase(
	name string,
	iterations int,
	maxSerialized int,
	cRoundtrip cRoundtripFn,
	goRoundtrip func(input []byte, output []byte) (int8, int, int8, int),
	rng *uint64,
) error {
	var input [maxIOBuffer]byte
	var cOutput [maxIOBuffer]byte
	var goOutput [maxIOBuffer]byte

	if maxSerialized <= 0 || maxSerialized > maxIOBuffer {
		return fmt.Errorf("%s serialization size exceeds harness buffer: %d", name, maxSerialized)
	}
	inputCeiling := maxSerialized + 8
	if inputCeiling > maxIOBuffer {
		inputCeiling = maxIOBuffer
	}

	for iter := 0; iter < iterations; iter++ {
		inputSize := int(nextRandomU32(rng) % uint32(inputCeiling+1))
		fillRandomBytes(input[:inputSize], rng)
		for i := 0; i < maxSerialized; i++ {
			cOutput[i] = 0xA5
			goOutput[i] = 0xA5
		}

		var cResult C.CCaseResult
		cStatus := cRoundtrip(
			(*C.uint8_t)(unsafe.Pointer(&input[0])),
			C.size_t(inputSize),
			(*C.uint8_t)(unsafe.Pointer(&cOutput[0])),
			C.size_t(maxSerialized),
			&cResult,
		)
		if cStatus != 0 {
			return fmt.Errorf("C harness failed in %s iter=%d status=%d", name, iter, int(cStatus))
		}

		goDesRC, goConsumed, goSerRC, goSerSize := goRoundtrip(input[:inputSize], goOutput[:maxSerialized])
		cDesRC := int8(cResult.deserialize_rc)
		cConsumed := int(cResult.deserialize_consumed)
		if goDesRC != cDesRC || goConsumed != cConsumed {
			return fmt.Errorf(
				"deserialize mismatch in %s iter=%d c(rc=%d,consumed=%d) go(rc=%d,consumed=%d) input=[%s]",
				name,
				iter,
				cDesRC,
				cConsumed,
				goDesRC,
				goConsumed,
				formatBytes(input[:inputSize]),
			)
		}
		if goDesRC < 0 {
			continue
		}

		cSerRC := int8(cResult.serialize_rc)
		cSerSize := int(cResult.serialize_size)
		if goSerRC != cSerRC || goSerSize != cSerSize {
			return fmt.Errorf(
				"serialize mismatch in %s iter=%d c(rc=%d,size=%d) go(rc=%d,size=%d)",
				name,
				iter,
				cSerRC,
				cSerSize,
				goSerRC,
				goSerSize,
			)
		}
		if !bytes.Equal(cOutput[:cSerSize], goOutput[:goSerSize]) {
			return fmt.Errorf(
				"serialize bytes mismatch in %s iter=%d c=[%s] go=[%s]",
				name,
				iter,
				formatBytes(cOutput[:cSerSize]),
				formatBytes(goOutput[:goSerSize]),
			)
		}
	}

	fmt.Printf("PASS %s random (%d iterations)\n", name, iterations)
	return nil
}

func runDirectedChecks() (int, map[string]int, error) {
	directedCount := 0
	directedCategoryCounts := map[string]int{}
	recordDirected := func(category string) {
		directedCount++
		directedCategoryCounts[category]++
	}

	checkDirectedSerialize := func(
		marker string,
		value int8,
		expectedByte byte,
		cFn func(C.int8_t, *C.uint8_t, C.size_t, *C.CCaseResult) C.int,
		goFn func(int8, []byte) (int8, int),
	) error {
		var cResult C.CCaseResult
		var cOut [8]byte
		var goOut [8]byte
		cStatus := cFn(C.int8_t(value), (*C.uint8_t)(unsafe.Pointer(&cOut[0])), 1, &cResult)
		if cStatus != 0 {
			return fmt.Errorf("C directed serialize failed in %s status=%d", marker, int(cStatus))
		}
		goRC, goSize := goFn(value, goOut[:1])
		if int8(cResult.serialize_rc) != goRC || int(cResult.serialize_size) != goSize {
			return fmt.Errorf(
				"directed serialize metadata mismatch in %s c(rc=%d,size=%d) go(rc=%d,size=%d)",
				marker,
				int8(cResult.serialize_rc),
				int(cResult.serialize_size),
				goRC,
				goSize,
			)
		}
		if cOut[0] != goOut[0] || cOut[0] != expectedByte {
			return fmt.Errorf(
				"directed serialize byte mismatch in %s c=%02X go=%02X expected=%02X",
				marker,
				cOut[0],
				goOut[0],
				expectedByte,
			)
		}
		fmt.Printf("PASS %s directed\n", marker)
		return nil
	}

	int3SatSerialize := func(value int8, out []byte) (int8, int) {
		obj := vendor.Int3Sat@V1_0@{Value: value}
		return obj.Serialize(out)
	}
	int3TruncSerialize := func(value int8, out []byte) (int8, int) {
		obj := vendor.Int3Trunc@V1_0@{Value: value}
		return obj.Serialize(out)
	}
	int3SatRoundtrip := func(input []byte, output []byte) (int8, int, int8, int) {
		var obj vendor.Int3Sat@V1_0@
		desRC, consumed := obj.Deserialize(input)
		if desRC < 0 {
			return desRC, consumed, 0, 0
		}
		serRC, serSize := obj.Serialize(output)
		return desRC, consumed, serRC, serSize
	}
	int3TruncRoundtrip := func(input []byte, output []byte) (int8, int, int8, int) {
		var obj vendor.Int3Trunc@V1_0@
		desRC, consumed := obj.Deserialize(input)
		if desRC < 0 {
			return desRC, consumed, 0, 0
		}
		serRC, serSize := obj.Serialize(output)
		return desRC, consumed, serRC, serSize
	}

	checkDirectedRoundtrip := func(
		name string,
		input []byte,
		outputCapacity int,
		expectDeserializeError bool,
		expectSerializeError bool,
		cFn cRoundtripFn,
		goFn func(input []byte, output []byte) (int8, int, int8, int),
	) error {
		var cIn [maxIOBuffer]byte
		var cOut [maxIOBuffer]byte
		var goOut [maxIOBuffer]byte
		if len(input) > len(cIn) {
			return fmt.Errorf("%s input too large: %d", name, len(input))
		}
		if outputCapacity < 0 || outputCapacity > len(cOut) {
			return fmt.Errorf("%s invalid output capacity: %d", name, outputCapacity)
		}
		copy(cIn[:], input)

		var cResult C.CCaseResult
		cStatus := cFn(
			(*C.uint8_t)(unsafe.Pointer(&cIn[0])),
			C.size_t(len(input)),
			(*C.uint8_t)(unsafe.Pointer(&cOut[0])),
			C.size_t(outputCapacity),
			&cResult,
		)
		if cStatus != 0 {
			return fmt.Errorf("%s C roundtrip failed with status=%d", name, int(cStatus))
		}

		goDesRC, goConsumed, goSerRC, goSerSize := goFn(input, goOut[:outputCapacity])
		cDesRC := int8(cResult.deserialize_rc)
		cConsumed := int(cResult.deserialize_consumed)
		cSerRC := int8(cResult.serialize_rc)
		cSerSize := int(cResult.serialize_size)

		if goDesRC != cDesRC || goConsumed != cConsumed {
			return fmt.Errorf(
				"%s deserialize mismatch C(rc=%d,consumed=%d) Go(rc=%d,consumed=%d)",
				name,
				cDesRC,
				cConsumed,
				goDesRC,
				goConsumed,
			)
		}

		if expectDeserializeError && cDesRC >= 0 {
			return fmt.Errorf("%s expected deserialize error, got rc=%d", name, cDesRC)
		}
		if !expectDeserializeError && cDesRC < 0 {
			return fmt.Errorf("%s expected deserialize success, got rc=%d", name, cDesRC)
		}
		if cDesRC < 0 {
			fmt.Printf("PASS %s directed\n", name)
			return nil
		}

		if goSerRC != cSerRC {
			return fmt.Errorf("%s serialize rc mismatch C=%d Go=%d", name, cSerRC, goSerRC)
		}
		if expectSerializeError && cSerRC >= 0 {
			return fmt.Errorf("%s expected serialize error, got rc=%d", name, cSerRC)
		}
		if !expectSerializeError && cSerRC < 0 {
			return fmt.Errorf("%s expected serialize success, got rc=%d", name, cSerRC)
		}
		if cSerRC >= 0 {
			if goSerSize != cSerSize {
				return fmt.Errorf("%s serialize size mismatch C=%d Go=%d", name, cSerSize, goSerSize)
			}
			if !bytes.Equal(cOut[:cSerSize], goOut[:goSerSize]) {
				return fmt.Errorf(
					"%s serialize bytes mismatch C=[%s] Go=[%s]",
					name,
					formatBytes(cOut[:cSerSize]),
					formatBytes(goOut[:goSerSize]),
				)
			}
		}

		fmt.Printf("PASS %s directed\n", name)
		return nil
	}

	if err := checkDirectedSerialize(
		"int3sat_serialize_plus7_saturated",
		7,
		0x03,
		func(v C.int8_t, out *C.uint8_t, cap C.size_t, result *C.CCaseResult) C.int {
			return C.c_int3sat_directed_serialize(v, out, cap, result)
		},
		int3SatSerialize,
	); err != nil {
		return directedCount, directedCategoryCounts, err
	}
	recordDirected("saturation_sign_extension")
	if err := checkDirectedSerialize(
		"int3sat_serialize_minus9_saturated",
		-9,
		0x04,
		func(v C.int8_t, out *C.uint8_t, cap C.size_t, result *C.CCaseResult) C.int {
			return C.c_int3sat_directed_serialize(v, out, cap, result)
		},
		int3SatSerialize,
	); err != nil {
		return directedCount, directedCategoryCounts, err
	}
	recordDirected("saturation_sign_extension")
	if err := checkDirectedSerialize(
		"int3trunc_serialize_plus5_truncated",
		5,
		0x05,
		func(v C.int8_t, out *C.uint8_t, cap C.size_t, result *C.CCaseResult) C.int {
			return C.c_int3trunc_directed_serialize(v, out, cap, result)
		},
		int3TruncSerialize,
	); err != nil {
		return directedCount, directedCategoryCounts, err
	}
	recordDirected("saturation_sign_extension")
	if err := checkDirectedSerialize(
		"int3trunc_serialize_minus5_truncated",
		-5,
		0x03,
		func(v C.int8_t, out *C.uint8_t, cap C.size_t, result *C.CCaseResult) C.int {
			return C.c_int3trunc_directed_serialize(v, out, cap, result)
		},
		int3TruncSerialize,
	); err != nil {
		return directedCount, directedCategoryCounts, err
	}
	recordDirected("saturation_sign_extension")

	for _, tv := range []struct {
		name                   string
		input                  []byte
		outputCapacity         int
		expectDeserializeError bool
		expectSerializeError   bool
		cFn                    cRoundtripFn
		goFn                   func(input []byte, output []byte) (int8, int, int8, int)
	}{
		{
			name:                   "int3sat_truncated_input",
			input:                  []byte{},
			outputCapacity:         vendor.INT3SAT_1_0_SERIALIZATION_BUFFER_SIZE_BYTES,
			expectDeserializeError: false,
			expectSerializeError:   false,
			cFn: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_int3sat_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goFn: int3SatRoundtrip,
		},
		{
			name:                   "int3trunc_truncated_input",
			input:                  []byte{},
			outputCapacity:         vendor.INT3TRUNC_1_0_SERIALIZATION_BUFFER_SIZE_BYTES,
			expectDeserializeError: false,
			expectSerializeError:   false,
			cFn: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_int3trunc_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goFn: int3TruncRoundtrip,
		},
		{
			name:                   "int3sat_serialize_small_buffer",
			input:                  []byte{0x00},
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   true,
			cFn: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_int3sat_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goFn: int3SatRoundtrip,
		},
		{
			name:                   "int3trunc_serialize_small_buffer",
			input:                  []byte{0x00},
			outputCapacity:         0,
			expectDeserializeError: false,
			expectSerializeError:   true,
			cFn: func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
				return C.c_int3trunc_roundtrip(input, inputSize, output, outputCapacity, result)
			},
			goFn: int3TruncRoundtrip,
		},
	} {
		if err := checkDirectedRoundtrip(
			tv.name,
			tv.input,
			tv.outputCapacity,
			tv.expectDeserializeError,
			tv.expectSerializeError,
			tv.cFn,
			tv.goFn,
		); err != nil {
			return directedCount, directedCategoryCounts, err
		}
		if strings.Contains(tv.name, "truncated_input") {
			recordDirected("truncation")
		} else {
			recordDirected("serialize_buffer")
		}
	}

	for _, tc := range []struct {
		name         string
		marker       string
		sample       byte
		expected     int8
		cDeserialize func(C.uint8_t, *C.int8_t, *C.CCaseResult) C.int
		goValue      func(byte) (int8, int8, int)
	}{
		{
			name:     "Int3Sat sign extension sample 0x07",
			marker:   "int3sat_sign_extend_0x07",
			sample:   0x07,
			expected: -1,
			cDeserialize: func(sample C.uint8_t, out *C.int8_t, result *C.CCaseResult) C.int {
				return C.c_int3sat_deserialize_value(sample, out, result)
			},
			goValue: func(sample byte) (int8, int8, int) {
				var obj vendor.Int3Sat@V1_0@
				rc, consumed := obj.Deserialize([]byte{sample})
				return rc, obj.Value, consumed
			},
		},
		{
			name:     "Int3Sat sign extension sample 0x04",
			marker:   "int3sat_sign_extend_0x04",
			sample:   0x04,
			expected: -4,
			cDeserialize: func(sample C.uint8_t, out *C.int8_t, result *C.CCaseResult) C.int {
				return C.c_int3sat_deserialize_value(sample, out, result)
			},
			goValue: func(sample byte) (int8, int8, int) {
				var obj vendor.Int3Sat@V1_0@
				rc, consumed := obj.Deserialize([]byte{sample})
				return rc, obj.Value, consumed
			},
		},
		{
			name:     "Int3Trunc sign extension sample 0x05",
			marker:   "int3trunc_sign_extend_0x05",
			sample:   0x05,
			expected: -3,
			cDeserialize: func(sample C.uint8_t, out *C.int8_t, result *C.CCaseResult) C.int {
				return C.c_int3trunc_deserialize_value(sample, out, result)
			},
			goValue: func(sample byte) (int8, int8, int) {
				var obj vendor.Int3Trunc@V1_0@
				rc, consumed := obj.Deserialize([]byte{sample})
				return rc, obj.Value, consumed
			},
		},
		{
			name:     "Int3Trunc sign extension sample 0x03",
			marker:   "int3trunc_sign_extend_0x03",
			sample:   0x03,
			expected: 3,
			cDeserialize: func(sample C.uint8_t, out *C.int8_t, result *C.CCaseResult) C.int {
				return C.c_int3trunc_deserialize_value(sample, out, result)
			},
			goValue: func(sample byte) (int8, int8, int) {
				var obj vendor.Int3Trunc@V1_0@
				rc, consumed := obj.Deserialize([]byte{sample})
				return rc, obj.Value, consumed
			},
		},
	} {
		var cValue C.int8_t
		var cResult C.CCaseResult
		cStatus := tc.cDeserialize(C.uint8_t(tc.sample), &cValue, &cResult)
		if cStatus != 0 {
			return directedCount, directedCategoryCounts, fmt.Errorf("%s C helper failed status=%d", tc.name, int(cStatus))
		}
		goRC, goValue, goConsumed := tc.goValue(tc.sample)
		if int8(cResult.deserialize_rc) != goRC || int(cResult.deserialize_consumed) != goConsumed || int8(cValue) != goValue || goValue != tc.expected {
			return directedCount, directedCategoryCounts, fmt.Errorf(
				"%s mismatch C(rc=%d,consumed=%d,value=%d) Go(rc=%d,consumed=%d,value=%d) expected=%d",
				tc.name,
				int8(cResult.deserialize_rc),
				int(cResult.deserialize_consumed),
				int8(cValue),
				goRC,
				goConsumed,
				goValue,
				tc.expected,
			)
		}
		fmt.Printf("PASS %s directed\n", tc.marker)
		recordDirected("saturation_sign_extension")
	}

	emitCategorySummary("directed", directedCategoryCounts)
	fmt.Println("PASS signed-narrow-directed")
	return directedCount, directedCategoryCounts, nil
}

func main() {
	iterations := 256
	if len(os.Args) > 1 {
		parsed, err := strconv.Atoi(os.Args[1])
		if err != nil || parsed <= 0 {
			fmt.Fprintf(os.Stderr, "invalid iteration count %q\n", os.Args[1])
			os.Exit(2)
		}
		iterations = parsed
	}

	rng := uint64(0xE6A1FD3BC9157A2D)
	if err := runRandomCase(
		"vendor.Int3Sat.1.0",
		iterations,
		vendor.INT3SAT_1_0_SERIALIZATION_BUFFER_SIZE_BYTES,
		func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
			return C.c_int3sat_roundtrip(input, inputSize, output, outputCapacity, result)
		},
		func(input []byte, output []byte) (int8, int, int8, int) {
			var obj vendor.Int3Sat@V1_0@
			desRC, consumed := obj.Deserialize(input)
			if desRC < 0 {
				return desRC, consumed, 0, 0
			}
			serRC, serSize := obj.Serialize(output)
			return desRC, consumed, serRC, serSize
		},
		&rng,
	); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}

	if err := runRandomCase(
		"vendor.Int3Trunc.1.0",
		iterations,
		vendor.INT3TRUNC_1_0_SERIALIZATION_BUFFER_SIZE_BYTES,
		func(input *C.uint8_t, inputSize C.size_t, output *C.uint8_t, outputCapacity C.size_t, result *C.CCaseResult) C.int {
			return C.c_int3trunc_roundtrip(input, inputSize, output, outputCapacity, result)
		},
		func(input []byte, output []byte) (int8, int, int8, int) {
			var obj vendor.Int3Trunc@V1_0@
			desRC, consumed := obj.Deserialize(input)
			if desRC < 0 {
				return desRC, consumed, 0, 0
			}
			serRC, serSize := obj.Serialize(output)
			return desRC, consumed, serRC, serSize
		},
		&rng,
	); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}

	directedCases, directedCategories, err := runDirectedChecks()
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	_ = directedCategories
	const randomCases = 2

	fmt.Printf(
		"PASS signed-narrow-c-go-parity inventory random_cases=%d directed_cases=%d\n",
		randomCases,
		directedCases,
	)

	fmt.Printf(
		"PASS signed-narrow-c-go-parity random_iterations=%d random_cases=%d directed_cases=%d\n",
		iterations,
		randomCases,
		directedCases,
	)
}
