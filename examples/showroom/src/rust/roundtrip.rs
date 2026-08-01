// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
// Shared by every Rust recipe in the matrix. See src/README.md for what these programs are for and
// what they deliberately do not test.
//
// Rust gets the shortest of the five, because the generated types derive PartialEq and Debug: the
// whole field-by-field comparison the C version spells out is one assert_eq!, and a mismatch prints
// both values without any help from here.

use lanyard::lanyard::health::subsystem_report_1_0::lanyard_health_SubsystemReport_1_0;
use lanyard::lanyard::health::system_health_1_0::lanyard_health_SystemHealth_1_0;

const SUBSYSTEMS: [&str; 3] = ["gnss", "esc.3", "imu.0"];

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
    let mut original = lanyard_health_SystemHealth_1_0::default();

    // Deliberately not the default value: a broken integration that serialised nothing and
    // deserialised nothing would round-trip a default struct perfectly and prove nothing.
    original.timestamp.microsecond = 1_234_567_890_123;
    original.aggregate_health.value = 2; // CAUTION, on the standard four-level scale

    for (index, name) in SUBSYSTEMS.iter().enumerate() {
        let mut report = lanyard_health_SubsystemReport_1_0::default();
        report.health.value = (index % 4) as u8;
        report.severity.value = (index % 8) as u8;
        report.fault_code = 0x1000 + index as u16;
        for byte in name.as_bytes() {
            report.name.push(*byte);
        }
        original.subsystem.push(report);
    }

    let mut buffer = [0u8; lanyard_health_SystemHealth_1_0::SERIALIZATION_BUFFER_SIZE_BYTES];
    let written = original
        .serialize(&mut buffer)
        .unwrap_or_else(|e| panic!("FAIL: serialize returned {e}"));

    let mut restored = lanyard_health_SystemHealth_1_0::default();
    restored
        .deserialize(&buffer[..written])
        .unwrap_or_else(|e| panic!("FAIL: deserialize returned {e}"));

    assert_eq!(restored, original, "FAIL: round-trip changed the value");

    println!(
        "round-trip OK: {} subsystems, {written} bytes on the wire",
        restored.subsystem.len()
    );
}
