#![allow(non_camel_case_types)]

//===----------------------------------------------------------------------===//
///
/// @file
/// Rust parity driver for signed-narrow C-vs-Rust compatibility tests.
///
/// The executable validates randomized and directed behaviour parity for signed
/// 3-bit saturating and truncating integer fixture types.
///
//===----------------------------------------------------------------------===//

use std::fmt::Write as _;
use std::os::raw::c_int;

use signed_narrow_generated::vendor::int3sat_1_0::vendor_Int3Sat_1_0;
use signed_narrow_generated::vendor::int3trunc_1_0::vendor_Int3Trunc_1_0;

const MAX_IO_BUFFER: usize = 64;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct CCaseResult {
    deserialize_rc: i8,
    deserialize_consumed: usize,
    serialize_rc: i8,
    serialize_size: usize,
}

type CRoundtripFn =
    unsafe extern "C" fn(*const u8, usize, *mut u8, usize, *mut CCaseResult) -> c_int;

unsafe extern "C" {
    fn c_int3sat_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;
    fn c_int3trunc_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;

    fn c_int3sat_directed_serialize(
        value: i8,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;
    fn c_int3trunc_directed_serialize(
        value: i8,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;

    fn c_int3sat_deserialize_value(sample: u8, out_value: *mut i8, result: *mut CCaseResult) -> c_int;
    fn c_int3trunc_deserialize_value(
        sample: u8,
        out_value: *mut i8,
        result: *mut CCaseResult,
    ) -> c_int;
}

fn next_random_u32(state: &mut u64) -> u32 {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    (*state & 0xFFFF_FFFF) as u32
}

fn fill_random_bytes(dst: &mut [u8], state: &mut u64) {
    for b in dst {
        *b = (next_random_u32(state) & 0xFF) as u8;
    }
}

fn format_bytes(data: &[u8]) -> String {
    let mut out = String::new();
    for (idx, b) in data.iter().enumerate() {
        if idx > 0 {
            out.push(' ');
        }
        let _ = write!(&mut out, "{:02X}", b);
    }
    out
}

fn run_case<T>(
    name: &str,
    iterations: usize,
    max_serialized: usize,
    c_roundtrip: CRoundtripFn,
    rust_deserialize: fn(&mut T, &[u8]) -> (i8, usize),
    rust_serialize: fn(&T, &mut [u8]) -> Result<usize, i8>,
    rng_state: &mut u64,
) -> Result<(), String>
where
    T: Default,
{
    let mut input = [0u8; MAX_IO_BUFFER];
    let mut c_output = [0xA5u8; MAX_IO_BUFFER];
    let mut rust_output = [0xA5u8; MAX_IO_BUFFER];
    let max_input = max_serialized.saturating_add(8);

    if max_serialized > MAX_IO_BUFFER || max_input > MAX_IO_BUFFER {
        return Err(format!(
            "Case {name} exceeds harness static buffer sizing: max_serialized={max_serialized}"
        ));
    }

    for iter in 0..iterations {
        let input_size = (next_random_u32(rng_state) as usize) % (max_input + 1);
        fill_random_bytes(&mut input[..input_size], rng_state);

        let mut c_result = CCaseResult::default();
        c_output.fill(0xA5);
        rust_output.fill(0xA5);
        let c_status = unsafe {
            c_roundtrip(
                input.as_ptr(),
                input_size,
                c_output.as_mut_ptr(),
                max_serialized,
                &mut c_result,
            )
        };
        if c_status != 0 {
            return Err(format!("C harness call failed in {name} iter={iter} status={c_status}"));
        }

        let mut obj = T::default();
        let (rust_des_rc, rust_consumed) = rust_deserialize(&mut obj, &input[..input_size]);

        if rust_des_rc != c_result.deserialize_rc || rust_consumed != c_result.deserialize_consumed {
            return Err(format!(
                "Deserialize mismatch in {name} iter={iter} C(rc={},consumed={}) Rust(rc={},consumed={}) input=[{}]",
                c_result.deserialize_rc,
                c_result.deserialize_consumed,
                rust_des_rc,
                rust_consumed,
                format_bytes(&input[..input_size])
            ));
        }

        if rust_des_rc < 0 {
            continue;
        }

        let rust_ser_result = rust_serialize(&obj, &mut rust_output[..max_serialized]);
        let (rust_ser_rc, rust_ser_size) = match rust_ser_result {
            Ok(size) => (0i8, size),
            Err(rc) => (rc, 0usize),
        };

        let size_mismatch = rust_ser_size != c_result.serialize_size;
        let rc_mismatch = rust_ser_rc != c_result.serialize_rc;
        let byte_mismatch = !size_mismatch
            && rust_output[..rust_ser_size] != c_output[..c_result.serialize_size];
        if rc_mismatch || size_mismatch || byte_mismatch {
            let c_slice_size = c_result.serialize_size.min(MAX_IO_BUFFER);
            return Err(format!(
                "Serialize mismatch in {name} iter={iter} C(rc={},size={}) Rust(rc={},size={}) input=[{}] c=[{}] rust=[{}]",
                c_result.serialize_rc,
                c_result.serialize_size,
                rust_ser_rc,
                rust_ser_size,
                format_bytes(&input[..input_size]),
                format_bytes(&c_output[..c_slice_size]),
                format_bytes(&rust_output[..rust_ser_size.min(MAX_IO_BUFFER)])
            ));
        }
    }

    println!("PASS {name} random ({iterations} iterations)");
    Ok(())
}

fn int3sat_deserialize(out: &mut vendor_Int3Sat_1_0, buffer: &[u8]) -> (i8, usize) {
    out.deserialize_with_consumed(buffer)
}

fn int3sat_serialize(obj: &vendor_Int3Sat_1_0, buffer: &mut [u8]) -> Result<usize, i8> {
    obj.serialize(buffer)
}

fn int3trunc_deserialize(out: &mut vendor_Int3Trunc_1_0, buffer: &[u8]) -> (i8, usize) {
    out.deserialize_with_consumed(buffer)
}

fn int3trunc_serialize(obj: &vendor_Int3Trunc_1_0, buffer: &mut [u8]) -> Result<usize, i8> {
    obj.serialize(buffer)
}

fn run_directed_checks() -> Result<(), String> {
    let mut c_result = CCaseResult::default();

    let check_directed_serialize = |name: &str,
                                    value: i8,
                                    expected_byte: u8,
                                    c_fn: unsafe extern "C" fn(i8, *mut u8, usize, *mut CCaseResult) -> c_int,
                                    rust_fn: fn(i8, &mut [u8]) -> Result<usize, i8>|
     -> Result<(), String> {
        let mut c_result_local = CCaseResult::default();
        let mut c_out_local = [0u8; 8];
        let mut r_out_local = [0u8; 8];
        let c_status = unsafe {
            c_fn(value, c_out_local.as_mut_ptr(), 1, &mut c_result_local)
        };
        if c_status != 0 {
            return Err(format!("C directed serialize call failed in {name} status={c_status}"));
        }

        let rust_size = rust_fn(value, &mut r_out_local[..1]).map_err(|rc| {
            format!("Rust directed serialize failed in {name} rc={rc}")
        })?;

        if c_result_local.serialize_rc != 0 || c_result_local.serialize_size != rust_size {
            return Err(format!(
                "Directed serialize metadata mismatch in {name}: C(rc={},size={}) Rust(size={})",
                c_result_local.serialize_rc, c_result_local.serialize_size, rust_size
            ));
        }

        if c_out_local[0] != r_out_local[0] || c_out_local[0] != expected_byte {
            return Err(format!(
                "Directed serialize byte mismatch in {name}: C={:02X} Rust={:02X} expected={:02X}",
                c_out_local[0], r_out_local[0], expected_byte
            ));
        }

        Ok(())
    };

    let sat_rust = |value: i8, buffer: &mut [u8]| -> Result<usize, i8> {
        let obj = vendor_Int3Sat_1_0 { value };
        obj.serialize(buffer)
    };
    let trunc_rust = |value: i8, buffer: &mut [u8]| -> Result<usize, i8> {
        let obj = vendor_Int3Trunc_1_0 { value };
        obj.serialize(buffer)
    };

    check_directed_serialize(
        "Int3Sat +7",
        7,
        0x03,
        c_int3sat_directed_serialize,
        sat_rust,
    )?;
    println!("INFO signed-narrow-c-rust directed marker int3sat_serialize_plus7_saturated");
    check_directed_serialize(
        "Int3Sat -9",
        -9,
        0x04,
        c_int3sat_directed_serialize,
        sat_rust,
    )?;
    println!("INFO signed-narrow-c-rust directed marker int3sat_serialize_minus9_saturated");
    check_directed_serialize(
        "Int3Trunc +5",
        5,
        0x05,
        c_int3trunc_directed_serialize,
        trunc_rust,
    )?;
    println!("INFO signed-narrow-c-rust directed marker int3trunc_serialize_plus5_truncated");
    check_directed_serialize(
        "Int3Trunc -5",
        -5,
        0x03,
        c_int3trunc_directed_serialize,
        trunc_rust,
    )?;
    println!("INFO signed-narrow-c-rust directed marker int3trunc_serialize_minus5_truncated");

    for (sample, expected) in [(0x07u8, -1i8), (0x04u8, -4i8)] {
        let mut c_value = 0i8;
        let c_status = unsafe { c_int3sat_deserialize_value(sample, &mut c_value, &mut c_result) };
        if c_status != 0 {
            return Err(format!("C int3sat deserialize helper failed sample={sample:02X}"));
        }
        if c_result.deserialize_rc != 0 || c_result.deserialize_consumed != 1 {
            return Err(format!(
                "C int3sat deserialize metadata mismatch sample={sample:02X} rc={} consumed={}",
                c_result.deserialize_rc, c_result.deserialize_consumed
            ));
        }

        let mut rust_obj = vendor_Int3Sat_1_0::default();
        let (rust_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&[sample]);
        if rust_rc != 0 || rust_consumed != 1 || rust_obj.value != c_value || rust_obj.value != expected {
            return Err(format!(
                "Int3Sat sign-extension mismatch sample={sample:02X} C(value={}) Rust(rc={},consumed={},value={}) expected={expected}",
                c_value, rust_rc, rust_consumed, rust_obj.value
            ));
        }
        if sample == 0x07u8 {
            println!("INFO signed-narrow-c-rust directed marker int3sat_sign_extend_0x07");
        } else {
            println!("INFO signed-narrow-c-rust directed marker int3sat_sign_extend_0x04");
        }
    }

    for (sample, expected) in [(0x05u8, -3i8), (0x03u8, 3i8)] {
        let mut c_value = 0i8;
        let c_status = unsafe { c_int3trunc_deserialize_value(sample, &mut c_value, &mut c_result) };
        if c_status != 0 {
            return Err(format!("C int3trunc deserialize helper failed sample={sample:02X}"));
        }
        if c_result.deserialize_rc != 0 || c_result.deserialize_consumed != 1 {
            return Err(format!(
                "C int3trunc deserialize metadata mismatch sample={sample:02X} rc={} consumed={}",
                c_result.deserialize_rc, c_result.deserialize_consumed
            ));
        }

        let mut rust_obj = vendor_Int3Trunc_1_0::default();
        let (rust_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&[sample]);
        if rust_rc != 0 || rust_consumed != 1 || rust_obj.value != c_value || rust_obj.value != expected {
            return Err(format!(
                "Int3Trunc sign-extension mismatch sample={sample:02X} C(value={}) Rust(rc={},consumed={},value={}) expected={expected}",
                c_value, rust_rc, rust_consumed, rust_obj.value
            ));
        }
        if sample == 0x05u8 {
            println!("INFO signed-narrow-c-rust directed marker int3trunc_sign_extend_0x05");
        } else {
            println!("INFO signed-narrow-c-rust directed marker int3trunc_sign_extend_0x03");
        }
    }

    println!("PASS signed_narrow_directed_c_rust directed");
    Ok(())
}

fn main() {
    const RANDOM_CASES: usize = 2;
    const DIRECTED_CASES: usize = 1;
    let mut iterations = 256usize;
    if let Some(arg) = std::env::args().nth(1) {
        iterations = arg.parse::<usize>().unwrap_or_else(|_| {
            eprintln!("Invalid iteration count: {arg}");
            std::process::exit(2);
        });
        if iterations == 0 {
            eprintln!("Iteration count must be > 0");
            std::process::exit(2);
        }
    }

    let mut rng_state = 0x5A8D_1F2E_C3B4_9AA1u64;

    run_case::<vendor_Int3Sat_1_0>(
        "vendor.Int3Sat.1.0",
        iterations,
        vendor_Int3Sat_1_0::SERIALIZATION_BUFFER_SIZE_BYTES,
        c_int3sat_roundtrip,
        int3sat_deserialize,
        int3sat_serialize,
        &mut rng_state,
    )
    .unwrap_or_else(|e| {
        eprintln!("{e}");
        std::process::exit(1);
    });

    run_case::<vendor_Int3Trunc_1_0>(
        "vendor.Int3Trunc.1.0",
        iterations,
        vendor_Int3Trunc_1_0::SERIALIZATION_BUFFER_SIZE_BYTES,
        c_int3trunc_roundtrip,
        int3trunc_deserialize,
        int3trunc_serialize,
        &mut rng_state,
    )
    .unwrap_or_else(|e| {
        eprintln!("{e}");
        std::process::exit(1);
    });

    run_directed_checks().unwrap_or_else(|e| {
        eprintln!("{e}");
        std::process::exit(1);
    });

    println!(
        "PASS signed-narrow-c-rust inventory random_cases={RANDOM_CASES} directed_cases={DIRECTED_CASES}"
    );
    println!(
        "PASS signed-narrow-c-rust parity random_iterations={iterations} random_cases={RANDOM_CASES} directed_cases={DIRECTED_CASES}"
    );
    println!("Signed narrow C/Rust parity PASS");
}
