// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
// Shared by every Rust cell in the matrix. See src/README.md for what these programs are for and
// what they deliberately do not test.
//
// Rust gets the shortest of the six, because the generated types derive PartialEq and Debug: the
// whole field-by-field comparison the C version spells out is one assert_eq!, and a mismatch prints
// both values without any help from here.

use kitbag::kitbag::mode_1_0::kitbag_Mode_1_0;
use kitbag::kitbag::reading_1_0::kitbag_Reading_1_0;
use kitbag::kitbag::sensor_frame_1_0::kitbag_SensorFrame_1_0;

fn main() {
    // Start from Default and fill it in, rather than writing a struct literal.
    //
    // This is not only style. A generated variable-length field is a DsdlVec carrying a memory
    // contract -- the memory mode, inline threshold, and pool class declared for that field -- and
    // Default is what installs the right one. `DsdlVec::new()` produces a *default* contract
    // instead, and because VarArray derives PartialEq over that metadata as well as over the
    // payload, a value built that way compares unequal to the same value after a round trip even
    // though every field a reader can see is identical. Build from Default and the question does
    // not arise.
    let mut original = kitbag_SensorFrame_1_0::default();

    // Deliberately not the default value: a broken integration that serialised nothing and
    // deserialised nothing would round-trip a default struct perfectly and prove nothing.
    original.timestamp.microsecond = 1_234_567_890_123;

    for i in 0..3u8 {
        original.readings.push(kitbag_Reading_1_0 {
            channel: i + 1,
            // Exactly representable in binary32, so comparing for equality after the round trip is
            // a statement about the wiring rather than about floating-point rounding.
            value: 0.5f32 * f32::from(i + 1),
            mode: kitbag_Mode_1_0 { value: 2 }, // ACTIVE
        });
    }

    for byte in b"imu.0" {
        original.source.push(*byte);
    }

    let mut buffer = [0u8; kitbag_SensorFrame_1_0::SERIALIZATION_BUFFER_SIZE_BYTES];
    let written = original
        .serialize(&mut buffer)
        .unwrap_or_else(|e| panic!("FAIL: serialize returned {e}"));

    let mut restored = kitbag_SensorFrame_1_0::default();
    restored
        .deserialize(&buffer[..written])
        .unwrap_or_else(|e| panic!("FAIL: deserialize returned {e}"));

    assert_eq!(restored, original, "FAIL: round-trip changed the value");

    println!(
        "round-trip OK: {} readings, {} source bytes, {} bytes on the wire",
        restored.readings.len(),
        restored.source.len(),
        written
    );
}
