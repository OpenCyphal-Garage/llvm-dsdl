//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

// Runtime forward/backward compatibility test for the generated TypeScript serializers. See
// ForwardCompatDriver.c for the rationale: round-trip tests only decode a buffer with the same type
// version that wrote it, so the delimited version-skew skip is never exercised. This decodes across two
// versions of a delimited composite (wire.nar.Inner has one field; wire.wid.Inner appends a second).

import { wire_nar_holder_1_0 as nar, wire_wid_holder_1_0 as wid } from "./index";

// Forward compatibility: wide writer -> narrow reader skips the appended y.
const wideIn: wid.Holder_1_0 = { inner: { x: 1, y: 2 }, tail: 99 };
const narrowOut = nar.deserializeHolder_1_0(wid.serializeHolder_1_0(wideIn));
if (narrowOut.value.inner.x !== 1 || narrowOut.value.tail !== 99) {
  throw new Error(`forward-compat mismatch x=${narrowOut.value.inner.x} tail=${narrowOut.value.tail}`);
}
console.log("fwdcompat_ok");

// Zero extension: narrow writer -> wide reader zero-fills the absent y.
const narrowIn: nar.Holder_1_0 = { inner: { x: 5 }, tail: 77 };
const wideOut = wid.deserializeHolder_1_0(nar.serializeHolder_1_0(narrowIn));
if (wideOut.value.inner.x !== 5 || wideOut.value.inner.y !== 0 || wideOut.value.tail !== 77) {
  throw new Error(
    `zero-extension mismatch x=${wideOut.value.inner.x} y=${wideOut.value.inner.y} tail=${wideOut.value.tail}`,
  );
}
console.log("zeroext_ok");
