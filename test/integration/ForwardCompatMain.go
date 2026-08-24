//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

// Runtime forward/backward compatibility test for the generated Go serializers. See
// ForwardCompatDriver.c for the rationale: round-trip tests only decode a buffer with the same type
// version that wrote it, so the delimited version-skew skip is never exercised. This decodes across two
// versions of a delimited composite (wire.nar.Inner has one field; wire.wid.Inner appends a second).
package main

import (
	"fmt"
	"os"

	nar "dsdlfwdcompat/wire/nar"
	wid "dsdlfwdcompat/wire/wid"
)

func fail(msg string, code int) {
	fmt.Println(msg)
	os.Exit(code)
}

func main() {
	// Forward compatibility: wide writer -> narrow reader skips the appended Y.
	wideIn := wid.Holder@V1_0@{Inner: wid.Inner@V1_0@{X: 1, Y: 2}, Tail: 99}
	buf := make([]byte, 64)
	rc, n := wideIn.Serialize(buf)
	if rc != 0 {
		fail("go_serialize_wide_failed", 2)
	}
	var narrowOut nar.Holder@V1_0@
	rc2, _ := narrowOut.Deserialize(buf[:n])
	if rc2 != 0 {
		fail("go_deserialize_narrow_failed", 3)
	}
	if narrowOut.Inner.X != 1 || narrowOut.Tail != 99 {
		fail(fmt.Sprintf("go_forward_compat_mismatch x=%d tail=%d", narrowOut.Inner.X, narrowOut.Tail), 4)
	}
	fmt.Println("fwdcompat_ok")

	// Zero extension: narrow writer -> wide reader zero-fills the absent Y.
	narrowIn := nar.Holder@V1_0@{Inner: nar.Inner@V1_0@{X: 5}, Tail: 77}
	buf2 := make([]byte, 64)
	rc3, n3 := narrowIn.Serialize(buf2)
	if rc3 != 0 {
		fail("go_serialize_narrow_failed", 5)
	}
	// Poison the destination so a missed zero-extension is visible rather than an incidental zero.
	wideOut := wid.Holder@V1_0@{Inner: wid.Inner@V1_0@{X: 0xEE, Y: 0xEE}, Tail: 0xEE}
	rc4, _ := wideOut.Deserialize(buf2[:n3])
	if rc4 != 0 {
		fail("go_deserialize_wide_failed", 6)
	}
	if wideOut.Inner.X != 5 || wideOut.Inner.Y != 0 || wideOut.Tail != 77 {
		fail(fmt.Sprintf("go_zero_extension_mismatch x=%d y=%d tail=%d", wideOut.Inner.X, wideOut.Inner.Y, wideOut.Tail), 7)
	}
	fmt.Println("zeroext_ok")
}
