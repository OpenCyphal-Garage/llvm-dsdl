//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

// Coverage-guided fuzz harness over the *generated Go deserializers* — the Go
// analogue of test/integration/NativeDecoderFuzz.c.
//
// Go is memory-safe, so the failure class differs from C: there is no OOB/UB to
// catch, but a decoder that indexes a slice out of range, dereferences a nil, or
// otherwise PANICS on attacker-controlled wire bytes is a real robustness / DoS
// defect. This harness feeds arbitrary bytes straight into each generated
// Deserialize entrypoint; the Go fuzzer turns any panic into a hard failure
// (and minimizes the reproducer). Accepted objects are round-tripped back
// through Serialize so that path is exercised on adversarially-shaped-but-valid
// inputs too.
//
// The curated type set mirrors the C fuzzer's adversarial shapes:
//   * Heartbeat            — simple fixed-size message (baseline)
//   * Health / Integer8    — narrow scalar width + signedness normalization
//   * Frame (union)        — tagged union dispatch (invalid tag rejection)
//   * ExecuteCommand.{Req,Resp} — variable-length arrays with length prefixes
//   * port.List            — nested delimited composites + union-of-composites
//                            + fixed-array-of-variable-array (deepest path)
//
// Run modes:
//   * `go test`                     — replays the committed seed corpus only
//                                     (deterministic, fast; the default CI lane).
//   * `go test -fuzz=FuzzDecoders`  — coverage-guided loop (deeper / cron lane).

package decoderfuzz

import (
	"testing"

	metacan "uavcan_dsdl_generated/uavcan/metatransport/can"
	node "uavcan_dsdl_generated/uavcan/node"
	nodeport "uavcan_dsdl_generated/uavcan/node/port"
	scalar "uavcan_dsdl_generated/uavcan/primitive/scalar"
)

// decodeAndRoundTrip runs one type's Deserialize on arbitrary bytes and, if the
// bytes are accepted, re-serializes into a correctly sized buffer. A panic in
// either path fails the fuzz test; a negative rc (rejection) is the expected,
// safe outcome for malformed input.
func decodeAndRoundTrip[T any](data []byte, bufSize int, deser func(*T, []byte) (int8, int), ser func(*T, []byte) (int8, int)) {
	var obj T
	rc, _ := deser(&obj, data)
	if rc < 0 {
		return
	}
	out := make([]byte, bufSize)
	_, _ = ser(&obj, out)
}

func fuzzOneInput(data []byte) {
	decodeAndRoundTrip(data, node.HEARTBEAT_1_0_SERIALIZATION_BUFFER_SIZE_BYTES,
		func(o *node.Heartbeat_1_0, b []byte) (int8, int) { return o.Deserialize(b) },
		func(o *node.Heartbeat_1_0, b []byte) (int8, int) { return o.Serialize(b) })

	decodeAndRoundTrip(data, node.HEALTH_1_0_SERIALIZATION_BUFFER_SIZE_BYTES,
		func(o *node.Health_1_0, b []byte) (int8, int) { return o.Deserialize(b) },
		func(o *node.Health_1_0, b []byte) (int8, int) { return o.Serialize(b) })

	decodeAndRoundTrip(data, scalar.INTEGER8_1_0_SERIALIZATION_BUFFER_SIZE_BYTES,
		func(o *scalar.Integer8_1_0, b []byte) (int8, int) { return o.Deserialize(b) },
		func(o *scalar.Integer8_1_0, b []byte) (int8, int) { return o.Serialize(b) })

	decodeAndRoundTrip(data, metacan.FRAME_0_2_SERIALIZATION_BUFFER_SIZE_BYTES,
		func(o *metacan.Frame_0_2, b []byte) (int8, int) { return o.Deserialize(b) },
		func(o *metacan.Frame_0_2, b []byte) (int8, int) { return o.Serialize(b) })

	decodeAndRoundTrip(data, node.EXECUTE_COMMAND_1_3_REQUEST_SERIALIZATION_BUFFER_SIZE_BYTES,
		func(o *node.ExecuteCommand_1_3_Request, b []byte) (int8, int) { return o.Deserialize(b) },
		func(o *node.ExecuteCommand_1_3_Request, b []byte) (int8, int) { return o.Serialize(b) })

	decodeAndRoundTrip(data, node.EXECUTE_COMMAND_1_3_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES,
		func(o *node.ExecuteCommand_1_3_Response, b []byte) (int8, int) { return o.Deserialize(b) },
		func(o *node.ExecuteCommand_1_3_Response, b []byte) (int8, int) { return o.Serialize(b) })

	decodeAndRoundTrip(data, nodeport.LIST_1_0_SERIALIZATION_BUFFER_SIZE_BYTES,
		func(o *nodeport.List_1_0, b []byte) (int8, int) { return o.Deserialize(b) },
		func(o *nodeport.List_1_0, b []byte) (int8, int) { return o.Serialize(b) })
}

func FuzzDecoders(f *testing.F) {
	// Seed corpus: empty, a few short prefixes, and larger zero/0xFF blocks that
	// straddle length-prefix and delimiter-header boundaries in the nested types.
	f.Add([]byte{})
	f.Add([]byte{0x00})
	f.Add([]byte{0xFF})
	f.Add(make([]byte, 7))
	seedFF := make([]byte, 64)
	for i := range seedFF {
		seedFF[i] = 0xFF
	}
	f.Add(seedFF)
	f.Add(make([]byte, 512))

	f.Fuzz(func(t *testing.T, data []byte) {
		fuzzOneInput(data)
	})
}
