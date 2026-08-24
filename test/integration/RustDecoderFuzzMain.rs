//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//! Robustness fuzz over the *generated Rust deserializers* — the Rust analogue of
//! test/integration/NativeDecoderFuzz.c and GoDecoderFuzzTest.go.
//!
//! Rust is memory-safe: an out-of-bounds slice access panics (a controlled abort)
//! rather than corrupting memory. But a generated decoder that PANICS on
//! attacker-controlled wire bytes is still a real robustness / DoS defect — the
//! contract is that arbitrary bytes must produce either a decoded object or a
//! clean `Err`, never a panic. This harness feeds a large, deterministic stream
//! of pseudo-random byte slices (plus structured edge seeds) into each generated
//! `deserialize` for the adversarial type set, wrapping every call in
//! `catch_unwind` so a panic is reported (type + input) and turned into a
//! non-zero exit instead of a silent abort. Accepted objects are round-tripped
//! back through `serialize`.
//!
//! Iteration count defaults to a fast, deterministic CI value and is overridable
//! via LLVMDSDL_RUST_FUZZ_ITERS for a deep / cron run.

use std::panic::{catch_unwind, AssertUnwindSafe};

use uavcan_dsdl_generated::uavcan::metatransport::can::frame_0_2::uavcan_metatransport_can_Frame_0_2;
use uavcan_dsdl_generated::uavcan::node::execute_command_1_3::{
    uavcan_node_ExecuteCommand_1_3_Request, uavcan_node_ExecuteCommand_1_3_Response,
};
use uavcan_dsdl_generated::uavcan::node::health_1_0::uavcan_node_Health_1_0;
use uavcan_dsdl_generated::uavcan::node::heartbeat_1_0::uavcan_node_Heartbeat_1_0;
use uavcan_dsdl_generated::uavcan::node::port::list_1_0::uavcan_node_port_List_1_0;
use uavcan_dsdl_generated::uavcan::primitive::scalar::integer8_1_0::uavcan_primitive_scalar_Integer8_1_0;

/// Deterministic xorshift64* PRNG so failures are reproducible.
struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        x.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }
    fn byte(&mut self) -> u8 {
        (self.next() & 0xFF) as u8
    }
}

/// Feeds one input into every decoder in the curated adversarial set.
macro_rules! fuzz_types {
    ($data:expr, $( $ty:ty ),+ $(,)?) => {
        $(
            {
                let mut obj = <$ty>::default();
                if obj.deserialize($data).is_ok() {
                    let mut out = vec![0u8; <$ty>::SERIALIZATION_BUFFER_SIZE_BYTES];
                    let _ = obj.serialize(&mut out);
                }
            }
        )+
    };
}

fn fuzz_one_input(data: &[u8]) {
    // Curated shapes mirroring the C/Go fuzzers: baseline fixed-size, narrow scalar,
    // tagged union, variable-length arrays, and the deep nested-delimited path.
    fuzz_types!(
        data,
        uavcan_node_Heartbeat@V1_0@,
        uavcan_node_Health@V1_0@,
        uavcan_primitive_scalar_Integer8@V1_0@,
        uavcan_metatransport_can_Frame@V0_2@,
        uavcan_node_ExecuteCommand_1_3_Request,
        uavcan_node_ExecuteCommand_1_3_Response,
        uavcan_node_port_List@V1_0@,
    );
}

fn run_input(label: &str, data: &[u8]) -> bool {
    let result = catch_unwind(AssertUnwindSafe(|| fuzz_one_input(data)));
    if result.is_err() {
        eprintln!(
            "rust decoder fuzz: PANIC on {} input ({} bytes): {:?}",
            label,
            data.len(),
            data
        );
        return false;
    }
    true
}

fn main() {
    // Suppress the default panic hook's noisy backtrace during expected-clean runs;
    // catch_unwind still reports the failing type + input above.
    std::panic::set_hook(Box::new(|_| {}));

    let iters: u64 = std::env::var("LLVMDSDL_RUST_FUZZ_ITERS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(200_000);

    // Structured edge seeds that straddle length-prefix / delimiter-header boundaries.
    let mut ok = true;
    ok &= run_input("seed:empty", &[]);
    ok &= run_input("seed:one-zero", &[0x00]);
    ok &= run_input("seed:one-ff", &[0xFF]);
    ok &= run_input("seed:ff-64", &[0xFFu8; 64]);
    ok &= run_input("seed:zero-512", &[0u8; 512]);

    let mut rng = Rng(0x1234_5678_9ABC_DEF0);
    for _ in 0..iters {
        // Random length up to a size that exceeds the largest curated buffer, so
        // both under- and over-length inputs are exercised.
        let len = (rng.next() % 640) as usize;
        let mut data = vec![0u8; len];
        for b in data.iter_mut() {
            *b = rng.byte();
        }
        if !run_input("random", &data) {
            ok = false;
            break;
        }
    }

    if !ok {
        std::process::exit(1);
    }
    println!("rust decoder fuzz: {} random iterations + seeds clean (no panic)", iters);
}
