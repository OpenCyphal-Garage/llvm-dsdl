//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//! Runtime forward/backward compatibility test for the generated Rust serializers. See
//! ForwardCompatDriver.c for the full rationale: round-trip tests only ever decode a buffer with the
//! same type version that wrote it, so the delimited version-skew skip is never exercised. This decodes
//! across two versions of a delimited composite (`wire.nar.Inner` has one field; `wire.wid.Inner`
//! appends a second).

use dsdl_generated::wire::nar::holder_1_0::wire_nar_Holder_1_0 as NarHolder;
use dsdl_generated::wire::nar::inner_1_0::wire_nar_Inner_1_0 as NarInner;
use dsdl_generated::wire::wid::holder_1_0::wire_wid_Holder_1_0 as WidHolder;
use dsdl_generated::wire::wid::inner_1_0::wire_wid_Inner_1_0 as WidInner;

fn main() {
    // Forward compatibility: wide writer -> narrow reader skips the appended `y`.
    let wide_in = WidHolder { inner: WidInner { x: 1, y: 2 }, tail: 99 };
    let mut wire = [0u8; 64];
    let n = wide_in.serialize(&mut wire).expect("serialize wide");
    let mut narrow_out = NarHolder::default();
    narrow_out
        .deserialize(&wire[..n])
        .expect("deserialize narrow");
    assert_eq!(narrow_out.inner.x, 1, "forward-compat inner.x");
    assert_eq!(narrow_out.tail, 99, "forward-compat tail");
    println!("fwdcompat_ok");

    // Zero extension: narrow writer -> wide reader zero-fills the absent `y`.
    let narrow_in = NarHolder {
        inner: NarInner { x: 5 },
        tail: 77,
    };
    let mut wire2 = [0u8; 64];
    let n2 = narrow_in.serialize(&mut wire2).expect("serialize narrow");
    // Poison the destination so a missed zero-extension is visible rather than an incidental zero.
    let mut wide_out = WidHolder {
        inner: WidInner { x: 0xEE, y: 0xEE },
        tail: 0xEE,
    };
    wide_out
        .deserialize(&wire2[..n2])
        .expect("deserialize wide");
    assert_eq!(wide_out.inner.x, 5, "zero-ext inner.x");
    assert_eq!(wide_out.inner.y, 0, "zero-ext inner.y");
    assert_eq!(wide_out.tail, 77, "zero-ext tail");
    println!("zeroext_ok");
}
