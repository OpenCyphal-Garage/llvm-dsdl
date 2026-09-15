//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//! Array-length prefixes decoded by the generated Rust: above the capacity, beyond the width of
//! `usize`, and at the capacity. The lane runs the binary on the host and type-checks this crate
//! for the 32-bit targets rustup has installed, so the cases live in a `no_std` library.

#![cfg_attr(not(feature = "std"), no_std)]

extern crate alloc;

use alloc::format;
use alloc::string::String;
use alloc::vec::Vec;

use prefixguard_generated::dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;
use prefixguard_generated::prefixguard::prefix32_1_0::prefixguard_Prefix32@V1_0@;
use prefixguard_generated::prefixguard::prefix64_1_0::prefixguard_Prefix64@V1_0@;

const BAD_ARRAY_LENGTH: i8 = -DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;

/// What one decode found.
#[derive(Debug, PartialEq, Eq)]
pub enum Verdict {
    Passed,
    Skipped(&'static str),
    Failed(String),
}

/// One decode and its verdict.
pub struct Outcome {
    pub case: &'static str,
    pub prefix: u64,
    pub verdict: Verdict,
}

/// A buffer opening with a little-endian prefix of `prefix_bytes` bytes followed by
/// `payload_bytes` zero bytes.
fn with_prefix(prefix: u64, prefix_bytes: usize, payload_bytes: usize) -> Vec<u8> {
    let mut buffer = Vec::with_capacity(prefix_bytes + payload_bytes);
    buffer.extend_from_slice(&prefix.to_le_bytes()[..prefix_bytes]);
    buffer.resize(prefix_bytes + payload_bytes, 0);
    buffer
}

/// The verdict for a decode that has to fail with a bad array length and leave the array empty.
fn expect_rejected(result: Result<usize, i8>, len_after: usize) -> Verdict {
    match result {
        Err(rc) if rc == BAD_ARRAY_LENGTH && len_after == 0 => Verdict::Passed,
        Err(rc) if rc == BAD_ARRAY_LENGTH => {
            Verdict::Failed(format!("array holds {len_after} elements after rejection"))
        }
        Err(rc) => Verdict::Failed(format!("rc = {rc}, want {BAD_ARRAY_LENGTH}")),
        Ok(consumed) => Verdict::Failed(format!(
            "decoded {consumed} bytes into {len_after} elements, want rejection"
        )),
    }
}

fn prefix32_rejects_above_capacity(prefix: u32) -> Outcome {
    let mut obj = prefixguard_Prefix32@V1_0@::default();
    let result = obj.deserialize(&with_prefix(u64::from(prefix), 4, 0));
    Outcome {
        case: "prefix32_rejects_above_capacity",
        prefix: u64::from(prefix),
        verdict: expect_rejected(result, obj.payload.len()),
    }
}

fn prefix32_accepts_capacity() -> Outcome {
    const CAPACITY: usize = 65536;
    let mut obj = prefixguard_Prefix32@V1_0@::default();
    let buffer = with_prefix(CAPACITY as u64, 4, CAPACITY);
    let want_consumed = prefixguard_Prefix32@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES;
    let verdict = match obj.deserialize(&buffer) {
        Ok(consumed) if consumed != want_consumed => {
            Verdict::Failed(format!("consumed {consumed} bytes, want {want_consumed}"))
        }
        Ok(_) if obj.payload.len() != CAPACITY => Verdict::Failed(format!(
            "payload holds {} elements, want {CAPACITY}",
            obj.payload.len()
        )),
        Ok(_) => Verdict::Passed,
        Err(rc) => Verdict::Failed(format!("rc = {rc}, want success")),
    };
    Outcome {
        case: "prefix32_accepts_capacity",
        prefix: CAPACITY as u64,
        verdict,
    }
}

fn prefix64_rejects_above_capacity(prefix: u64) -> Outcome {
    let mut obj = prefixguard_Prefix64@V1_0@::default();
    let result = obj.deserialize(&with_prefix(prefix, 8, 0));
    Outcome {
        case: "prefix64_rejects_above_capacity",
        prefix,
        verdict: expect_rejected(result, obj.flags.len()),
    }
}

/// A length within the type's capacity that `usize` cannot hold is rejected as a bad array
/// length. A 64-bit `usize` holds every length this type allows, so the case is skipped there.
fn prefix64_rejects_beyond_index(prefix: u64) -> Outcome {
    let case = "prefix64_rejects_beyond_index";
    if (usize::MAX as u64) >= (1u64 << 33) {
        return Outcome {
            case,
            prefix,
            verdict: Verdict::Skipped("usize holds every length Prefix64 allows"),
        };
    }
    let mut obj = prefixguard_Prefix64@V1_0@::default();
    let result = obj.deserialize(&with_prefix(prefix, 8, 0));
    Outcome {
        case,
        prefix,
        verdict: expect_rejected(result, obj.flags.len()),
    }
}

fn prefix64_accepts_small_length() -> Outcome {
    let mut obj = prefixguard_Prefix64@V1_0@::default();
    let mut buffer = with_prefix(3, 8, 1);
    buffer[8] = 0x05;
    let want = [true, false, true];
    let verdict = match obj.deserialize(&buffer) {
        Ok(consumed) if consumed != buffer.len() => {
            Verdict::Failed(format!("consumed {consumed} bytes, want {}", buffer.len()))
        }
        Ok(_) if obj.flags.as_slice() != &want[..] => {
            Verdict::Failed(format!("flags = {:?}, want {want:?}", obj.flags.as_slice()))
        }
        Ok(_) => Verdict::Passed,
        Err(rc) => Verdict::Failed(format!("rc = {rc}, want success")),
    };
    Outcome {
        case: "prefix64_accepts_small_length",
        prefix: 3,
        verdict,
    }
}

/// Runs every case and returns the outcomes in a fixed order.
pub fn run_all() -> Vec<Outcome> {
    let mut outcomes = Vec::new();
    for prefix in [65537u32, 1 << 31, u32::MAX] {
        outcomes.push(prefix32_rejects_above_capacity(prefix));
    }
    outcomes.push(prefix32_accepts_capacity());
    for prefix in [(1u64 << 33) + 1, (1u64 << 33) + 3, 1u64 << 63, u64::MAX] {
        outcomes.push(prefix64_rejects_above_capacity(prefix));
    }
    for prefix in [1u64 << 32, (1u64 << 32) + 3, 1u64 << 33] {
        outcomes.push(prefix64_rejects_beyond_index(prefix));
    }
    outcomes.push(prefix64_accepts_small_length());
    outcomes
}
