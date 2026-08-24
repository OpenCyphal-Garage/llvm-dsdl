#![allow(non_camel_case_types)]

//===----------------------------------------------------------------------===//
///
/// @file
/// Rust parity driver comparing generated Rust and C serializers/deserializers.
///
/// The executable runs randomized and directed parity checks through a shared C
/// harness and validates status, consumed size, and payload equivalence.
///
//===----------------------------------------------------------------------===//

use std::fmt::Write as _;
use std::os::raw::c_int;

use uavcan_dsdl_generated::dsdl_runtime;
use uavcan_dsdl_generated::uavcan::metatransport::can::frame_0_2::uavcan_metatransport_can_Frame_0_2;
use uavcan_dsdl_generated::uavcan::node::execute_command_1_3::{
    uavcan_node_ExecuteCommand_1_3_Request, uavcan_node_ExecuteCommand_1_3_Response,
};
use uavcan_dsdl_generated::uavcan::node::heartbeat_1_0::uavcan_node_Heartbeat_1_0;
use uavcan_dsdl_generated::uavcan::node::health_1_0::uavcan_node_Health_1_0;
use uavcan_dsdl_generated::uavcan::node::port::list_1_0::uavcan_node_port_List_1_0;
use uavcan_dsdl_generated::uavcan::node::port::subject_id_1_0::uavcan_node_port_SubjectID_1_0;
use uavcan_dsdl_generated::uavcan::primitive::scalar::integer8_1_0::uavcan_primitive_scalar_Integer8_1_0;
use uavcan_dsdl_generated::uavcan::primitive::scalar::real32_1_0::uavcan_primitive_scalar_Real32_1_0;
use uavcan_dsdl_generated::uavcan::register::value_1_0::uavcan_register_Value_1_0;
use uavcan_dsdl_generated::uavcan::time::synchronized_timestamp_1_0::uavcan_time_SynchronizedTimestamp_1_0;

const MAX_IO_BUFFER: usize = 2048;

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
    fn c_heartbeat_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;
    fn c_health_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;
    fn c_synchronized_timestamp_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;
    fn c_integer8_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;

    fn c_execute_command_request_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;

    fn c_execute_command_response_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;

    fn c_frame_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;

    fn c_value_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;

    fn c_real32_roundtrip(
        input: *const u8,
        input_size: usize,
        output: *mut u8,
        output_capacity: usize,
        result: *mut CCaseResult,
    ) -> c_int;

    fn c_frame_bad_union_tag_deserialize(result: *mut CCaseResult) -> c_int;
    fn c_execute_response_bad_array_length_deserialize(result: *mut CCaseResult) -> c_int;
    fn c_list_bad_delimiter_header_deserialize(result: *mut CCaseResult) -> c_int;
    fn c_heartbeat_empty_deserialize(result: *mut CCaseResult) -> c_int;
    fn c_list_nested_bad_union_tag_deserialize(result: *mut CCaseResult) -> c_int;
    fn c_list_second_delimiter_bad_deserialize(result: *mut CCaseResult) -> c_int;
    fn c_list_second_section_nested_bad_union_tag_deserialize(result: *mut CCaseResult) -> c_int;
    fn c_list_third_delimiter_bad_deserialize(result: *mut CCaseResult) -> c_int;
    fn c_list_nested_bad_array_length_serialize(result: *mut CCaseResult) -> c_int;
    fn c_frame_bad_union_tag_serialize(result: *mut CCaseResult) -> c_int;
    fn c_execute_response_bad_array_length_serialize(result: *mut CCaseResult) -> c_int;
    fn c_execute_request_bad_array_length_serialize(result: *mut CCaseResult) -> c_int;
    fn c_execute_request_too_small_serialize(result: *mut CCaseResult) -> c_int;
    fn c_heartbeat_too_small_serialize(result: *mut CCaseResult) -> c_int;
    fn c_health_saturated_serialize(
        result: *mut CCaseResult,
        output: *mut u8,
        output_capacity: usize,
    ) -> c_int;
    fn c_synchronized_timestamp_truncated_serialize(
        result: *mut CCaseResult,
        output: *mut u8,
        output_capacity: usize,
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
    compare_bytes: bool,
    rng_state: &mut u64,
) -> Result<(), String>
where
    T: Default,
{
    let mut input = [0u8; MAX_IO_BUFFER];
    let mut c_output = [0xA5u8; MAX_IO_BUFFER];
    let mut rust_output = [0xA5u8; MAX_IO_BUFFER];
    let max_input = max_serialized.saturating_add(16);

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

        if rust_des_rc != c_result.deserialize_rc {
            return Err(format!(
                "Deserialize mismatch in {name} iter={iter} input_size={input_size} C(rc={},consumed={}) Rust(rc={},consumed={}) input=[{}]",
                c_result.deserialize_rc,
                c_result.deserialize_consumed,
                rust_des_rc,
                rust_consumed,
                format_bytes(&input[..input_size])
            ));
        }

        if rust_consumed != c_result.deserialize_consumed {
            return Err(format!(
                "Deserialize consumed-size mismatch in {name} iter={iter} input_size={input_size} C(consumed={}) Rust(consumed={}) input=[{}]",
                c_result.deserialize_consumed,
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
        let byte_mismatch = compare_bytes
            && !size_mismatch
            && rust_output[..rust_ser_size] != c_output[..c_result.serialize_size];
        if rc_mismatch || size_mismatch || byte_mismatch {
            let c_slice_size = c_result.serialize_size.min(MAX_IO_BUFFER);
            return Err(format!(
                "Serialize mismatch in {name} iter={iter} \
                 C(rc={},size={}) Rust(rc={},size={}) input=[{}] c=[{}] rust=[{}]",
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

fn heartbeat_deserialize(
    out: &mut uavcan_node_Heartbeat@V1_0@,
    buffer: &[u8],
) -> (i8, usize) {
    out.deserialize_with_consumed(buffer)
}

fn heartbeat_serialize(obj: &uavcan_node_Heartbeat@V1_0@, buffer: &mut [u8]) -> Result<usize, i8> {
    obj.serialize(buffer)
}

fn health_deserialize(out: &mut uavcan_node_Health@V1_0@, buffer: &[u8]) -> (i8, usize) {
    out.deserialize_with_consumed(buffer)
}

fn health_serialize(obj: &uavcan_node_Health@V1_0@, buffer: &mut [u8]) -> Result<usize, i8> {
    obj.serialize(buffer)
}

fn synchronized_timestamp_deserialize(
    out: &mut uavcan_time_SynchronizedTimestamp@V1_0@,
    buffer: &[u8],
) -> (i8, usize) {
    out.deserialize_with_consumed(buffer)
}

fn synchronized_timestamp_serialize(
    obj: &uavcan_time_SynchronizedTimestamp@V1_0@,
    buffer: &mut [u8],
) -> Result<usize, i8> {
    obj.serialize(buffer)
}

fn integer8_deserialize(out: &mut uavcan_primitive_scalar_Integer8@V1_0@, buffer: &[u8]) -> (i8, usize) {
    out.deserialize_with_consumed(buffer)
}

fn integer8_serialize(
    obj: &uavcan_primitive_scalar_Integer8@V1_0@,
    buffer: &mut [u8],
) -> Result<usize, i8> {
    obj.serialize(buffer)
}

fn execute_request_deserialize(
    out: &mut uavcan_node_ExecuteCommand_1_3_Request,
    buffer: &[u8],
) -> (i8, usize) {
    out.deserialize_with_consumed(buffer)
}

fn execute_request_serialize(
    obj: &uavcan_node_ExecuteCommand_1_3_Request,
    buffer: &mut [u8],
) -> Result<usize, i8> {
    obj.serialize(buffer)
}

fn execute_response_deserialize(
    out: &mut uavcan_node_ExecuteCommand_1_3_Response,
    buffer: &[u8],
) -> (i8, usize) {
    out.deserialize_with_consumed(buffer)
}

fn execute_response_serialize(
    obj: &uavcan_node_ExecuteCommand_1_3_Response,
    buffer: &mut [u8],
) -> Result<usize, i8> {
    obj.serialize(buffer)
}

fn frame_deserialize(
    out: &mut uavcan_metatransport_can_Frame@V0_2@,
    buffer: &[u8],
) -> (i8, usize) {
    out.deserialize_with_consumed(buffer)
}

fn frame_serialize(
    obj: &uavcan_metatransport_can_Frame@V0_2@,
    buffer: &mut [u8],
) -> Result<usize, i8> {
    obj.serialize(buffer)
}

fn value_deserialize(out: &mut uavcan_register_Value@V1_0@, buffer: &[u8]) -> (i8, usize) {
    out.deserialize_with_consumed(buffer)
}

fn value_serialize(obj: &uavcan_register_Value@V1_0@, buffer: &mut [u8]) -> Result<usize, i8> {
    obj.serialize(buffer)
}

fn run_directed_error_cases() -> Result<(), String> {
    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_heartbeat_empty_deserialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for heartbeat empty-input deserialize: status={c_status}"
            ));
        }
        let mut rust_obj = uavcan_node_Heartbeat@V1_0@::default();
        let (rust_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&[]);
        if rust_rc != 0 {
            return Err(format!(
                "Rust heartbeat empty-input deserialize unexpectedly failed rc={rust_rc}"
            ));
        }
        if c_result.deserialize_rc != 0
            || c_result.deserialize_consumed != rust_consumed
            || rust_consumed != 0
        {
            return Err(format!(
                "Directed mismatch (Heartbeat empty-input deserialize): C(rc={},consumed={}) Rust(rc={},consumed={})",
                c_result.deserialize_rc, c_result.deserialize_consumed, rust_rc, rust_consumed
            ));
        }
        println!("INFO c/rust directed marker heartbeat_empty_deserialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_frame_bad_union_tag_deserialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for frame bad-union-tag deserialize: status={c_status}"
            ));
        }
        let mut rust_obj = uavcan_metatransport_can_Frame@V0_2@::default();
        let (rust_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&[0xFFu8]);
        if rust_rc >= 0 {
            return Err(format!(
                "Rust frame bad-union-tag deserialize unexpectedly succeeded consumed={rust_consumed}"
            ));
        }
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG;
        if c_result.deserialize_rc != rust_rc
            || c_result.deserialize_consumed != rust_consumed
            || rust_rc != expected
        {
            return Err(format!(
                "Directed mismatch (Frame bad union-tag deserialize): C(rc={},consumed={}) Rust(rc={},consumed={})",
                c_result.deserialize_rc, c_result.deserialize_consumed, rust_rc, rust_consumed
            ));
        }
        println!("INFO c/rust directed marker frame_bad_union_tag_deserialize");
    }

    {
        // Service request mixed-path: declared count exceeds payload bytes but remains representable.
        // Deserialization should succeed via truncation/zero-extension and reserialize deterministically.
        let input = [0x34u8, 0x12u8, 0x02u8, 0xAAu8];
        let mut c_result = CCaseResult::default();
        let mut c_output = [0xA5u8; MAX_IO_BUFFER];
        let c_status = unsafe {
            c_execute_command_request_roundtrip(
                input.as_ptr(),
                input.len(),
                c_output.as_mut_ptr(),
                uavcan_node_ExecuteCommand_1_3_Request::SERIALIZATION_BUFFER_SIZE_BYTES,
                &mut c_result,
            )
        };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for execute-request truncated-payload roundtrip: status={c_status}"
            ));
        }
        if c_result.deserialize_rc != 0 {
            return Err(format!(
                "C execute-request truncated-payload unexpectedly failed deserialize rc={}",
                c_result.deserialize_rc
            ));
        }

        let mut rust_obj = uavcan_node_ExecuteCommand_1_3_Request::default();
        let (rust_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&input);
        if rust_rc != 0 {
            return Err(format!(
                "Rust execute-request truncated-payload deserialize unexpectedly failed rc={rust_rc}"
            ));
        }
        if rust_consumed != c_result.deserialize_consumed {
            return Err(format!(
                "Directed mismatch (ExecuteCommand.Request truncated-payload consumed): \
                 C(consumed={}) Rust(consumed={})",
                c_result.deserialize_consumed, rust_consumed
            ));
        }

        let mut rust_output =
            vec![0xA5u8; uavcan_node_ExecuteCommand_1_3_Request::SERIALIZATION_BUFFER_SIZE_BYTES];
        let rust_ser_size = match rust_obj.serialize(&mut rust_output) {
            Ok(size) => size,
            Err(rc) => {
                return Err(format!(
                    "Rust execute-request truncated-payload serialize unexpectedly failed rc={rc}"
                ));
            }
        };
        if c_result.serialize_rc != 0 || rust_ser_size != c_result.serialize_size {
            return Err(format!(
                "Directed mismatch (ExecuteCommand.Request truncated-payload serialize size/rc): \
                 C(rc={},size={}) Rust(size={})",
                c_result.serialize_rc, c_result.serialize_size, rust_ser_size
            ));
        }
        if rust_output[..rust_ser_size] != c_output[..c_result.serialize_size] {
            return Err(format!(
                "Directed mismatch (ExecuteCommand.Request truncated-payload serialize bytes): \
                 C=[{}] Rust=[{}]",
                format_bytes(&c_output[..c_result.serialize_size]),
                format_bytes(&rust_output[..rust_ser_size])
            ));
        }
        println!("INFO c/rust directed marker execute_request_truncated_payload_roundtrip");
    }

    {
        // Service response mixed-path: declared count exceeds payload bytes but remains representable.
        // Deserialization should succeed via truncation/zero-extension and reserialize deterministically.
        let input = [0x01u8, 0x02u8, 0xAAu8];
        let mut c_result = CCaseResult::default();
        let mut c_output = [0xA5u8; MAX_IO_BUFFER];
        let c_status = unsafe {
            c_execute_command_response_roundtrip(
                input.as_ptr(),
                input.len(),
                c_output.as_mut_ptr(),
                uavcan_node_ExecuteCommand_1_3_Response::SERIALIZATION_BUFFER_SIZE_BYTES,
                &mut c_result,
            )
        };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for execute-response truncated-payload roundtrip: status={c_status}"
            ));
        }
        if c_result.deserialize_rc != 0 {
            return Err(format!(
                "C execute-response truncated-payload unexpectedly failed deserialize rc={}",
                c_result.deserialize_rc
            ));
        }

        let mut rust_obj = uavcan_node_ExecuteCommand_1_3_Response::default();
        let (rust_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&input);
        if rust_rc != 0 {
            return Err(format!(
                "Rust execute-response truncated-payload deserialize unexpectedly failed rc={rust_rc}"
            ));
        }
        if rust_consumed != c_result.deserialize_consumed {
            return Err(format!(
                "Directed mismatch (ExecuteCommand.Response truncated-payload consumed): \
                 C(consumed={}) Rust(consumed={})",
                c_result.deserialize_consumed, rust_consumed
            ));
        }

        let mut rust_output =
            vec![0xA5u8; uavcan_node_ExecuteCommand_1_3_Response::SERIALIZATION_BUFFER_SIZE_BYTES];
        let rust_ser_size = match rust_obj.serialize(&mut rust_output) {
            Ok(size) => size,
            Err(rc) => {
                return Err(format!(
                    "Rust execute-response truncated-payload serialize unexpectedly failed rc={rc}"
                ));
            }
        };
        if c_result.serialize_rc != 0 || rust_ser_size != c_result.serialize_size {
            return Err(format!(
                "Directed mismatch (ExecuteCommand.Response truncated-payload serialize size/rc): \
                 C(rc={},size={}) Rust(size={})",
                c_result.serialize_rc, c_result.serialize_size, rust_ser_size
            ));
        }
        if rust_output[..rust_ser_size] != c_output[..c_result.serialize_size] {
            return Err(format!(
                "Directed mismatch (ExecuteCommand.Response truncated-payload serialize bytes): \
                 C=[{}] Rust=[{}]",
                format_bytes(&c_output[..c_result.serialize_size]),
                format_bytes(&rust_output[..rust_ser_size])
            ));
        }
        println!("INFO c/rust directed marker execute_response_truncated_payload_roundtrip");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_execute_response_bad_array_length_deserialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for execute-response bad-array-length deserialize: status={c_status}"
            ));
        }
        let mut rust_obj = uavcan_node_ExecuteCommand_1_3_Response::default();
        let (rust_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&[0x00u8, 0xFFu8]);
        if rust_rc >= 0 {
            return Err(format!(
                "Rust execute-response bad-array-length deserialize unexpectedly succeeded consumed={rust_consumed}"
            ));
        }
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;
        if c_result.deserialize_rc != rust_rc
            || c_result.deserialize_consumed != rust_consumed
            || rust_rc != expected
        {
            return Err(format!(
                "Directed mismatch (ExecuteCommand.Response bad array-length deserialize): \
                 C(rc={},consumed={}) Rust(rc={},consumed={})",
                c_result.deserialize_rc, c_result.deserialize_consumed, rust_rc, rust_consumed
            ));
        }
        println!("INFO c/rust directed marker execute_response_bad_array_length_deserialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_list_bad_delimiter_header_deserialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for list bad-delimiter-header deserialize: status={c_status}"
            ));
        }
        let mut rust_obj = uavcan_node_port_List@V1_0@::default();
        let (rust_rc, rust_consumed) =
            rust_obj.deserialize_with_consumed(&[0xFFu8, 0xFFu8, 0xFFu8, 0x7Fu8]);
        if rust_rc >= 0 {
            return Err(format!(
                "Rust list bad-delimiter-header deserialize unexpectedly succeeded consumed={rust_consumed}"
            ));
        }
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER;
        if c_result.deserialize_rc != rust_rc
            || c_result.deserialize_consumed != rust_consumed
            || rust_rc != expected
        {
            return Err(format!(
                "Directed mismatch (List bad delimiter-header deserialize): \
                 C(rc={},consumed={}) Rust(rc={},consumed={})",
                c_result.deserialize_rc, c_result.deserialize_consumed, rust_rc, rust_consumed
            ));
        }
        println!("INFO c/rust directed marker list_bad_delimiter_header_deserialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_list_nested_bad_union_tag_deserialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for list nested bad-union-tag deserialize: status={c_status}"
            ));
        }
        let mut rust_obj = uavcan_node_port_List@V1_0@::default();
        let (rust_rc, rust_consumed) =
            rust_obj.deserialize_with_consumed(&[0x01u8, 0x00u8, 0x00u8, 0x00u8, 0xFFu8]);
        if rust_rc >= 0 {
            return Err(format!(
                "Rust list nested bad-union-tag deserialize unexpectedly succeeded consumed={rust_consumed}"
            ));
        }
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG;
        if c_result.deserialize_rc != rust_rc
            || c_result.deserialize_consumed != rust_consumed
            || rust_rc != expected
        {
            return Err(format!(
                "Directed mismatch (List nested bad union-tag deserialize): \
                 C(rc={},consumed={}) Rust(rc={},consumed={})",
                c_result.deserialize_rc, c_result.deserialize_consumed, rust_rc, rust_consumed
            ));
        }
        println!("INFO c/rust directed marker list_nested_bad_union_tag_deserialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_list_second_delimiter_bad_deserialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for list second-delimiter deserialize: status={c_status}"
            ));
        }
        let mut rust_obj = uavcan_node_port_List@V1_0@::default();
        let (rust_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&[
            0x00u8, 0x00u8, 0x00u8, 0x00u8, 0xFFu8, 0xFFu8, 0xFFu8, 0x7Fu8,
        ]);
        if rust_rc >= 0 {
            return Err(format!(
                "Rust list second-delimiter deserialize unexpectedly succeeded consumed={rust_consumed}"
            ));
        }
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER;
        if c_result.deserialize_rc != rust_rc
            || c_result.deserialize_consumed != rust_consumed
            || rust_rc != expected
        {
            return Err(format!(
                "Directed mismatch (List second delimiter-header deserialize): \
                 C(rc={},consumed={}) Rust(rc={},consumed={})",
                c_result.deserialize_rc, c_result.deserialize_consumed, rust_rc, rust_consumed
            ));
        }
        println!("INFO c/rust directed marker list_second_delimiter_header_deserialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status =
            unsafe { c_list_second_section_nested_bad_union_tag_deserialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for list second-section nested bad-union-tag deserialize: status={c_status}"
            ));
        }
        let mut rust_obj = uavcan_node_port_List@V1_0@::default();
        let (rust_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&[
            0x00u8, 0x00u8, 0x00u8, 0x00u8, 0x01u8, 0x00u8, 0x00u8, 0x00u8, 0xFFu8,
        ]);
        if rust_rc >= 0 {
            return Err(format!(
                "Rust list second-section nested bad-union-tag deserialize unexpectedly succeeded consumed={rust_consumed}"
            ));
        }
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG;
        if c_result.deserialize_rc != rust_rc
            || c_result.deserialize_consumed != rust_consumed
            || rust_rc != expected
        {
            return Err(format!(
                "Directed mismatch (List second-section nested bad union-tag deserialize): \
                 C(rc={},consumed={}) Rust(rc={},consumed={})",
                c_result.deserialize_rc, c_result.deserialize_consumed, rust_rc, rust_consumed
            ));
        }
        println!("INFO c/rust directed marker list_second_section_nested_bad_union_tag_deserialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_list_third_delimiter_bad_deserialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for list third-delimiter deserialize: status={c_status}"
            ));
        }
        let mut rust_obj = uavcan_node_port_List@V1_0@::default();
        let (rust_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&[
            0x00u8, 0x00u8, 0x00u8, 0x00u8, 0x00u8, 0x00u8, 0x00u8, 0x00u8, 0xFFu8, 0xFFu8,
            0xFFu8, 0x7Fu8,
        ]);
        if rust_rc >= 0 {
            return Err(format!(
                "Rust list third-delimiter deserialize unexpectedly succeeded consumed={rust_consumed}"
            ));
        }
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_DELIMITER_HEADER;
        if c_result.deserialize_rc != rust_rc
            || c_result.deserialize_consumed != rust_consumed
            || rust_rc != expected
        {
            return Err(format!(
                "Directed mismatch (List third delimiter-header deserialize): \
                 C(rc={},consumed={}) Rust(rc={},consumed={})",
                c_result.deserialize_rc, c_result.deserialize_consumed, rust_rc, rust_consumed
            ));
        }
        println!("INFO c/rust directed marker list_third_delimiter_header_deserialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_list_nested_bad_array_length_serialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for list nested bad-array-length serialize: status={c_status}"
            ));
        }
        let mut rust_buffer = vec![0u8; uavcan_node_port_List@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES];
        let mut rust_obj = uavcan_node_port_List@V1_0@::default();
        rust_obj.publishers._tag_ = 1u8;
        rust_obj
            .publishers
            .sparse_list
            .resize(256usize, uavcan_node_port_SubjectID@V1_0@::default());
        let rust_rc = match rust_obj.serialize(&mut rust_buffer) {
            Ok(size) => {
                return Err(format!(
                    "Rust list nested bad-array-length serialize unexpectedly succeeded size={size}"
                ));
            }
            Err(rc) => rc,
        };
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;
        if c_result.serialize_rc != rust_rc || rust_rc != expected {
            return Err(format!(
                "Directed mismatch (List nested bad array-length serialize): \
                 C(rc={},size={}) Rust(rc={})",
                c_result.serialize_rc, c_result.serialize_size, rust_rc
            ));
        }
        println!("INFO c/rust directed marker list_nested_bad_array_length_serialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_frame_bad_union_tag_serialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for frame bad-union-tag serialize: status={c_status}"
            ));
        }
        let mut rust_buffer =
            vec![0u8; uavcan_metatransport_can_Frame@V0_2@::SERIALIZATION_BUFFER_SIZE_BYTES];
        let rust_obj = uavcan_metatransport_can_Frame@V0_2@ {
            _tag_: 0xFFu8,
            ..Default::default()
        };
        let rust_rc = match rust_obj.serialize(&mut rust_buffer) {
            Ok(size) => {
                return Err(format!(
                    "Rust frame bad-union-tag serialize unexpectedly succeeded size={size}"
                ));
            }
            Err(rc) => rc,
        };
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_UNION_TAG;
        if c_result.serialize_rc != rust_rc || rust_rc != expected {
            return Err(format!(
                "Directed mismatch (Frame bad union-tag serialize): C(rc={},size={}) Rust(rc={})",
                c_result.serialize_rc, c_result.serialize_size, rust_rc
            ));
        }
        println!("INFO c/rust directed marker frame_bad_union_tag_serialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_execute_response_bad_array_length_serialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for execute-response bad-array-length serialize: status={c_status}"
            ));
        }
        let mut rust_buffer =
            vec![0u8; uavcan_node_ExecuteCommand_1_3_Response::SERIALIZATION_BUFFER_SIZE_BYTES];
        let mut rust_obj = uavcan_node_ExecuteCommand_1_3_Response::default();
        rust_obj.output.resize(47usize, 0u8);
        let rust_rc = match rust_obj.serialize(&mut rust_buffer) {
            Ok(size) => {
                return Err(format!(
                    "Rust execute-response bad-array-length serialize unexpectedly succeeded size={size}"
                ));
            }
            Err(rc) => rc,
        };
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;
        if c_result.serialize_rc != rust_rc || rust_rc != expected {
            return Err(format!(
                "Directed mismatch (ExecuteCommand.Response bad array-length serialize): \
                 C(rc={},size={}) Rust(rc={})",
                c_result.serialize_rc, c_result.serialize_size, rust_rc
            ));
        }
        println!("INFO c/rust directed marker execute_response_bad_array_length_serialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_execute_request_bad_array_length_serialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for execute-request bad-array-length serialize: status={c_status}"
            ));
        }
        let mut rust_buffer =
            vec![0u8; uavcan_node_ExecuteCommand_1_3_Request::SERIALIZATION_BUFFER_SIZE_BYTES];
        let mut rust_obj = uavcan_node_ExecuteCommand_1_3_Request::default();
        rust_obj.parameter.resize(256usize, 0u8);
        let rust_rc = match rust_obj.serialize(&mut rust_buffer) {
            Ok(size) => {
                return Err(format!(
                    "Rust execute-request bad-array-length serialize unexpectedly succeeded size={size}"
                ));
            }
            Err(rc) => rc,
        };
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;
        if c_result.serialize_rc != rust_rc || rust_rc != expected {
            return Err(format!(
                "Directed mismatch (ExecuteCommand.Request bad array-length serialize): \
                 C(rc={},size={}) Rust(rc={})",
                c_result.serialize_rc, c_result.serialize_size, rust_rc
            ));
        }
        println!("INFO c/rust directed marker execute_request_bad_array_length_serialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_execute_request_too_small_serialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for execute-request too-small serialize: status={c_status}"
            ));
        }
        let mut rust_buffer = vec![
            0u8;
            uavcan_node_ExecuteCommand_1_3_Request::SERIALIZATION_BUFFER_SIZE_BYTES
                .saturating_sub(1)
        ];
        let rust_obj = uavcan_node_ExecuteCommand_1_3_Request::default();
        let rust_rc = match rust_obj.serialize(&mut rust_buffer) {
            Ok(size) => {
                return Err(format!(
                    "Rust execute-request too-small serialize unexpectedly succeeded size={size}"
                ));
            }
            Err(rc) => rc,
        };
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
        if c_result.serialize_rc != rust_rc || rust_rc != expected {
            return Err(format!(
                "Directed mismatch (ExecuteCommand.Request too-small serialize): \
                 C(rc={},size={}) Rust(rc={})",
                c_result.serialize_rc, c_result.serialize_size, rust_rc
            ));
        }
        println!("INFO c/rust directed marker execute_request_too_small_serialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let c_status = unsafe { c_heartbeat_too_small_serialize(&mut c_result) };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for heartbeat too-small serialize: status={c_status}"
            ));
        }
        let mut rust_buffer =
            vec![0u8; uavcan_node_Heartbeat@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES.saturating_sub(1)];
        let rust_obj = uavcan_node_Heartbeat@V1_0@::default();
        let rust_rc = match rust_obj.serialize(&mut rust_buffer) {
            Ok(size) => {
                return Err(format!(
                    "Rust heartbeat too-small serialize unexpectedly succeeded size={size}"
                ));
            }
            Err(rc) => rc,
        };
        let expected = -dsdl_runtime::DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
        if c_result.serialize_rc != rust_rc || rust_rc != expected {
            return Err(format!(
                "Directed mismatch (Heartbeat too-small serialize): C(rc={},size={}) Rust(rc={})",
                c_result.serialize_rc, c_result.serialize_size, rust_rc
            ));
        }
        println!("INFO c/rust directed marker heartbeat_too_small_serialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let mut c_output = [0u8; MAX_IO_BUFFER];
        let c_status = unsafe {
            c_health_saturated_serialize(
                &mut c_result,
                c_output.as_mut_ptr(),
                uavcan_node_Health@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
            )
        };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for health saturated serialize: status={c_status}"
            ));
        }
        let rust_obj = uavcan_node_Health@V1_0@ { value: 0xFFu8 };
        let mut rust_output = vec![0u8; uavcan_node_Health@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES];
        let rust_size = match rust_obj.serialize(&mut rust_output) {
            Ok(size) => size,
            Err(rc) => {
                return Err(format!(
                    "Rust health saturated serialize unexpectedly failed rc={rc}"
                ));
            }
        };
        if c_result.serialize_rc != 0
            || rust_size != c_result.serialize_size
            || rust_size != 1
            || c_output[0] != rust_output[0]
            || c_output[0] != 0x03u8
        {
            return Err(format!(
                "Directed mismatch (Health saturating serialize): C(rc={},size={},byte={:02X}) Rust(size={},byte={:02X})",
                c_result.serialize_rc,
                c_result.serialize_size,
                c_output[0],
                rust_size,
                rust_output[0]
            ));
        }
        println!("INFO c/rust directed marker health_saturating_serialize");
    }

    {
        let mut c_result = CCaseResult::default();
        let mut c_output = [0u8; MAX_IO_BUFFER];
        let c_status = unsafe {
            c_synchronized_timestamp_truncated_serialize(
                &mut c_result,
                c_output.as_mut_ptr(),
                uavcan_time_SynchronizedTimestamp@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
            )
        };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for synchronized-timestamp truncated serialize: status={c_status}"
            ));
        }
        let rust_obj = uavcan_time_SynchronizedTimestamp@V1_0@ {
            microsecond: 0xFEDC_BA98_7654_3210u64,
        };
        let mut rust_output =
            vec![0u8; uavcan_time_SynchronizedTimestamp@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES];
        let rust_size = match rust_obj.serialize(&mut rust_output) {
            Ok(size) => size,
            Err(rc) => {
                return Err(format!(
                    "Rust synchronized-timestamp truncated serialize unexpectedly failed rc={rc}"
                ));
            }
        };
        let expected = [0x10u8, 0x32u8, 0x54u8, 0x76u8, 0x98u8, 0xBAu8, 0xDCu8];
        if c_result.serialize_rc != 0
            || rust_size != c_result.serialize_size
            || rust_size != expected.len()
            || c_output[..expected.len()] != expected
            || rust_output[..expected.len()] != expected
            || c_output[..expected.len()] != rust_output[..expected.len()]
        {
            return Err(format!(
                "Directed mismatch (SynchronizedTimestamp truncating serialize): \
                 C(rc={},size={},bytes=[{}]) Rust(size={},bytes=[{}])",
                c_result.serialize_rc,
                c_result.serialize_size,
                format_bytes(&c_output[..c_result.serialize_size.min(MAX_IO_BUFFER)]),
                rust_size,
                format_bytes(&rust_output[..rust_size.min(MAX_IO_BUFFER)])
            ));
        }
        println!("INFO c/rust directed marker synchronized_timestamp_truncating_serialize");
    }

    {
        for input_byte in [0x80u8, 0xFFu8] {
            let input = [input_byte];
            let mut c_result = CCaseResult::default();
            let mut c_output = [0u8; MAX_IO_BUFFER];
            let c_status = unsafe {
                c_integer8_roundtrip(
                    input.as_ptr(),
                    input.len(),
                    c_output.as_mut_ptr(),
                    uavcan_primitive_scalar_Integer8@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
                    &mut c_result,
                )
            };
            if c_status != 0 {
                return Err(format!(
                    "C harness call failed for Integer8 signed roundtrip input={input_byte:#04X}: status={c_status}"
                ));
            }
            let mut rust_obj = uavcan_primitive_scalar_Integer8@V1_0@::default();
            let (rust_des_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&input);
            if rust_des_rc != c_result.deserialize_rc
                || rust_consumed != c_result.deserialize_consumed
            {
                return Err(format!(
                    "Directed mismatch (Integer8 signed deserialize input={input_byte:#04X}): \
                     C(rc={},consumed={}) Rust(rc={},consumed={})",
                    c_result.deserialize_rc,
                    c_result.deserialize_consumed,
                    rust_des_rc,
                    rust_consumed
                ));
            }
            let mut rust_output =
                vec![0u8; uavcan_primitive_scalar_Integer8@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES];
            let rust_size = match rust_obj.serialize(&mut rust_output) {
                Ok(size) => size,
                Err(rc) => {
                    return Err(format!(
                        "Rust Integer8 signed serialize unexpectedly failed input={input_byte:#04X} rc={rc}"
                    ));
                }
            };
            if c_result.serialize_rc != 0
                || rust_size != c_result.serialize_size
                || rust_size != 1usize
                || c_output[0] != rust_output[0]
                || rust_output[0] != input_byte
            {
                return Err(format!(
                    "Directed mismatch (Integer8 signed serialize input={input_byte:#04X}): \
                     C(rc={},size={},byte={:02X}) Rust(size={},byte={:02X})",
                    c_result.serialize_rc,
                    c_result.serialize_size,
                    c_output[0],
                    rust_size,
                    rust_output[0]
                ));
            }
        }
        println!("INFO c/rust directed marker integer8_signed_roundtrip");
    }

    {
        // Float32 signaling-NaN payload must survive deserialize->serialize byte-exactly in both C
        // and Rust. Regression guard for the float32 -> f64 -> float32 round-trip that quieted
        // signaling NaNs (the quiet bit 0x40 in byte[2] must stay clear, i.e. 0x80 not 0xC0).
        // float16 is intentionally excluded: the shared runtime canonicalizes half-precision NaN
        // payloads to 0x7E00 for every backend, so there is no stable payload to preserve there.
        let golden = [0x01u8, 0x00u8, 0x80u8, 0x7Fu8]; // float32 sNaN
        let mut c_result = CCaseResult::default();
        let mut c_output = [0u8; MAX_IO_BUFFER];
        let c_status = unsafe {
            c_real32_roundtrip(
                golden.as_ptr(),
                golden.len(),
                c_output.as_mut_ptr(),
                uavcan_primitive_scalar_Real32@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
                &mut c_result,
            )
        };
        if c_status != 0 {
            return Err(format!(
                "C harness call failed for real32 signaling-NaN roundtrip: status={c_status}"
            ));
        }
        let mut rust_obj = uavcan_primitive_scalar_Real32@V1_0@::default();
        let (rust_des_rc, rust_consumed) = rust_obj.deserialize_with_consumed(&golden);
        let mut rust_output =
            vec![0u8; uavcan_primitive_scalar_Real32@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES];
        let rust_size = match rust_obj.serialize(&mut rust_output) {
            Ok(size) => size,
            Err(rc) => {
                return Err(format!(
                    "Rust real32 signaling-NaN serialize unexpectedly failed rc={rc}"
                ));
            }
        };
        if rust_des_rc != 0
            || rust_consumed != c_result.deserialize_consumed
            || c_result.serialize_rc != 0
            || rust_size != c_result.serialize_size
            || rust_size != golden.len()
            || c_output[..golden.len()] != golden
            || rust_output[..golden.len()] != golden
        {
            return Err(format!(
                "Directed mismatch (Real32 signaling-NaN payload roundtrip): golden=[{}] \
                 C(rc={},size={},bytes=[{}]) Rust(rc={},size={},bytes=[{}])",
                format_bytes(&golden),
                c_result.serialize_rc,
                c_result.serialize_size,
                format_bytes(&c_output[..c_result.serialize_size.min(MAX_IO_BUFFER)]),
                rust_des_rc,
                rust_size,
                format_bytes(&rust_output[..rust_size.min(MAX_IO_BUFFER)])
            ));
        }
        println!("INFO c/rust directed marker real32_signaling_nan_payload_roundtrip");
    }

    println!("PASS directed_error_parity directed");
    Ok(())
}

fn parse_iterations() -> Result<usize, String> {
    let mut args = std::env::args();
    let _program = args.next();
    if let Some(iterations) = args.next() {
        let parsed = iterations
            .parse::<usize>()
            .map_err(|_| format!("Invalid iteration count: {iterations}"))?;
        if parsed == 0 {
            return Err(format!("Invalid iteration count: {iterations}"));
        }
        return Ok(parsed);
    }
    Ok(128)
}

fn main() {
    const RANDOM_CASES: usize = 8;
    const DIRECTED_CASES: usize = 1;
    let iterations = match parse_iterations() {
        Ok(v) => v,
        Err(msg) => {
            eprintln!("{msg}");
            std::process::exit(2);
        }
    };

    let mut rng_state = 0xD6E8_FEB8_6659_FD93u64;

    let result = run_case(
        "uavcan.node.Heartbeat.1.0",
        iterations,
        uavcan_node_Heartbeat@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
        c_heartbeat_roundtrip,
        heartbeat_deserialize,
        heartbeat_serialize,
        true,
        &mut rng_state,
    )
    .and_then(|_| {
        run_case(
            "uavcan.node.ExecuteCommand.Request.1.3",
            iterations,
            uavcan_node_ExecuteCommand_1_3_Request::SERIALIZATION_BUFFER_SIZE_BYTES,
            c_execute_command_request_roundtrip,
            execute_request_deserialize,
            execute_request_serialize,
            true,
            &mut rng_state,
        )
    })
    .and_then(|_| {
        run_case(
            "uavcan.node.ExecuteCommand.Response.1.3",
            iterations,
            uavcan_node_ExecuteCommand_1_3_Response::SERIALIZATION_BUFFER_SIZE_BYTES,
            c_execute_command_response_roundtrip,
            execute_response_deserialize,
            execute_response_serialize,
            true,
            &mut rng_state,
        )
    })
    .and_then(|_| {
        run_case(
            "uavcan.metatransport.can.Frame.0.2",
            iterations,
            uavcan_metatransport_can_Frame@V0_2@::SERIALIZATION_BUFFER_SIZE_BYTES,
            c_frame_roundtrip,
            frame_deserialize,
            frame_serialize,
            true,
            &mut rng_state,
        )
    })
    .and_then(|_| {
        run_case(
            "uavcan.register.Value.1.0",
            iterations,
            uavcan_register_Value@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
            c_value_roundtrip,
            value_deserialize,
            value_serialize,
            true,
            &mut rng_state,
        )
    })
    .and_then(|_| {
        run_case(
            "uavcan.node.Health.1.0",
            iterations,
            uavcan_node_Health@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
            c_health_roundtrip,
            health_deserialize,
            health_serialize,
            true,
            &mut rng_state,
        )
    })
    .and_then(|_| {
        run_case(
            "uavcan.time.SynchronizedTimestamp.1.0",
            iterations,
            uavcan_time_SynchronizedTimestamp@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
            c_synchronized_timestamp_roundtrip,
            synchronized_timestamp_deserialize,
            synchronized_timestamp_serialize,
            true,
            &mut rng_state,
        )
    })
    .and_then(|_| {
        run_case(
            "uavcan.primitive.scalar.Integer8.1.0",
            iterations,
            uavcan_primitive_scalar_Integer8@V1_0@::SERIALIZATION_BUFFER_SIZE_BYTES,
            c_integer8_roundtrip,
            integer8_deserialize,
            integer8_serialize,
            true,
            &mut rng_state,
        )
    })
    .and_then(|_| run_directed_error_cases());

    match result {
        Ok(()) => {
            println!(
                "PASS c/rust inventory random_cases={RANDOM_CASES} directed_cases={DIRECTED_CASES}"
            );
            println!(
                "PASS c/rust parity random_iterations={iterations} random_cases={RANDOM_CASES} directed_cases={DIRECTED_CASES}"
            );
            println!("C/Rust parity PASS");
        }
        Err(msg) => {
            eprintln!("{msg}");
            std::process::exit(1);
        }
    }
}
