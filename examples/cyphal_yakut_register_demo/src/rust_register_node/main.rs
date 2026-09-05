//===----------------------------------------------------------------------===//
///
/// @file
/// Rust Cyphal node demo exposing heartbeat and register services.
///
/// This node uses llvm-dsdl-generated Rust types for serialisation and a
/// C transport shim around libudpard + POSIX UDP.
///
//===----------------------------------------------------------------------===//
use std::cmp;
use std::env;
use std::ffi::CString;
use std::os::raw::{c_char, c_int};
use std::ptr;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use cyphal_yakut_demo_generated_rust::uavcan::node::health_1_0::uavcan_node_Health_1_0;
use cyphal_yakut_demo_generated_rust::uavcan::node::heartbeat_1_0::uavcan_node_Heartbeat_1_0;
use cyphal_yakut_demo_generated_rust::uavcan::node::mode_1_0::uavcan_node_Mode_1_0;
use cyphal_yakut_demo_generated_rust::uavcan::primitive::array::natural16_1_0::uavcan_primitive_array_Natural16_1_0;
use cyphal_yakut_demo_generated_rust::uavcan::primitive::array::natural32_1_0::uavcan_primitive_array_Natural32_1_0;
use cyphal_yakut_demo_generated_rust::uavcan::primitive::string_1_0::uavcan_primitive_String_1_0;
use cyphal_yakut_demo_generated_rust::uavcan::register::access_1_0::{
    uavcan_register_Access_1_0_Request, uavcan_register_Access_1_0_Response,
};
use cyphal_yakut_demo_generated_rust::uavcan::register::list_1_0::{
    uavcan_register_List_1_0_Request, uavcan_register_List_1_0_Response,
};
use cyphal_yakut_demo_generated_rust::uavcan::register::name_1_0::uavcan_register_Name_1_0;
use cyphal_yakut_demo_generated_rust::uavcan::register::value_1_0::uavcan_register_Value_1_0;
use cyphal_yakut_demo_generated_rust::uavcan::time::synchronized_timestamp_1_0::uavcan_time_SynchronizedTimestamp_1_0;

const SUBJECT_HEARTBEAT: u16 = 7509;
const SERVICE_REGISTER_ACCESS: u16 = 384;
const SERVICE_REGISTER_LIST: u16 = 385;
const TX_QUEUE_CAPACITY: u32 = 128;
const RX_DATAGRAM_CAPACITY: usize = 2048;
const TX_DEADLINE_USEC: u64 = 1_000_000;
const MAX_WAIT_USEC: u64 = 50_000;

const VALUE_TAG_EMPTY: u8 = 0;
const VALUE_TAG_STRING: u8 = 1;
const VALUE_TAG_INTEGER64: u8 = 4;
const VALUE_TAG_INTEGER32: u8 = 5;
const VALUE_TAG_INTEGER16: u8 = 6;
const VALUE_TAG_INTEGER8: u8 = 7;
const VALUE_TAG_NATURAL64: u8 = 8;
const VALUE_TAG_NATURAL32: u8 = 9;
const VALUE_TAG_NATURAL16: u8 = 10;
const VALUE_TAG_NATURAL8: u8 = 11;

#[repr(C)]
struct GoDemoNode {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Default)]
struct GoDemoRpcTransfer {
    service_id: u16,
    source_node_id: u16,
    transfer_id: u8,
    payload: *mut u8,
    payload_size: usize,
}

unsafe extern "C" {
    fn go_demo_node_create() -> *mut GoDemoNode;
    fn go_demo_node_destroy(node: *mut GoDemoNode);
    fn go_demo_node_id_max() -> u16;
    fn go_demo_priority_nominal() -> u8;
    fn go_demo_node_init(
        node: *mut GoDemoNode,
        node_id: u16,
        iface_address: *const c_char,
        register_access_service_id: u16,
        register_access_request_extent: usize,
        register_list_service_id: u16,
        register_list_request_extent: usize,
        tx_queue_capacity: u32,
        rx_datagram_capacity: usize,
    ) -> c_int;
    fn go_demo_node_get_rpc_endpoint(
        node: *const GoDemoNode,
        out_ip_address: *mut u32,
        out_udp_port: *mut u16,
    ) -> c_int;
    fn go_demo_node_publish(
        node: *mut GoDemoNode,
        subject_id: u16,
        transfer_id: u8,
        payload: *const u8,
        payload_size: usize,
        deadline_usec: u64,
        priority: u8,
    ) -> c_int;
    fn go_demo_node_respond(
        node: *mut GoDemoNode,
        service_id: u16,
        destination_node_id: u16,
        transfer_id: u8,
        payload: *const u8,
        payload_size: usize,
        deadline_usec: u64,
        priority: u8,
    ) -> c_int;
    fn go_demo_node_poll_rpc(
        node: *mut GoDemoNode,
        timeout_usec: u64,
        out_transfer: *mut GoDemoRpcTransfer,
    ) -> c_int;
    fn go_demo_node_release_transfer(transfer: *mut GoDemoRpcTransfer);
    fn go_demo_node_pump_tx(node: *mut GoDemoNode);
    fn go_demo_now_usec() -> u64;
    fn go_demo_node_shutdown(node: *mut GoDemoNode) -> c_int;
}

#[derive(Clone)]
struct Options {
    name: String,
    node_id: u16,
    iface_address: String,
    heartbeat_rate_hz: u32,
}

enum ParseResult {
    Success(Options),
    Help,
    Error,
}

#[derive(Clone, Copy, Eq, PartialEq)]
enum RegisterKind {
    Natural16,
    Natural32,
    String,
}

#[derive(Clone)]
struct RegisterEntry {
    name: String,
    kind: RegisterKind,
    mutable_: bool,
    persistent: bool,
    natural16: u16,
    natural32: u32,
    string_value: String,
}

struct NodeApp {
    options: Options,
    node: *mut GoDemoNode,
    heartbeat_transfer_id: u8,
    started_at_usec: u64,
    heartbeat_period_usec: u64,
    next_heartbeat_at: u64,
    heartbeat_counter: u32,
    registers: Vec<RegisterEntry>,
}

impl NodeApp {
    fn new(options: Options) -> Self {
        Self {
            options,
            node: ptr::null_mut(),
            heartbeat_transfer_id: 0,
            started_at_usec: 0,
            heartbeat_period_usec: 1_000_000,
            next_heartbeat_at: 0,
            heartbeat_counter: 0,
            registers: Vec::new(),
        }
    }

    fn initialize(&mut self) -> Result<(), String> {
        let iface_cstr = CString::new(self.options.iface_address.clone())
            .map_err(|_| "Invalid --iface: contains NUL byte".to_string())?;

        self.node = unsafe { go_demo_node_create() };
        if self.node.is_null() {
            return Err("go_demo_node_create failed".to_string());
        }

        let init_rc = unsafe {
            go_demo_node_init(
                self.node,
                self.options.node_id,
                iface_cstr.as_ptr(),
                SERVICE_REGISTER_ACCESS,
                uavcan_register_Access_1_0_Request::EXTENT_BYTES,
                SERVICE_REGISTER_LIST,
                uavcan_register_List_1_0_Request::EXTENT_BYTES,
                TX_QUEUE_CAPACITY,
                RX_DATAGRAM_CAPACITY,
            )
        };
        if init_rc < 0 {
            return Err(format!("go_demo_node_init failed: {}", init_rc));
        }

        self.initialize_registers();
        self.update_heartbeat_period_from_registers();

        self.started_at_usec = unsafe { go_demo_now_usec() };
        self.next_heartbeat_at = self
            .started_at_usec
            .saturating_add(self.heartbeat_period_usec);

        let mut endpoint_ip: u32 = 0;
        let mut endpoint_port: u16 = 0;
        let endpoint_rc = unsafe {
            go_demo_node_get_rpc_endpoint(
                self.node,
                &mut endpoint_ip as *mut u32,
                &mut endpoint_port as *mut u16,
            )
        };
        if endpoint_rc < 0 {
            return Err("go_demo_node_get_rpc_endpoint failed".to_string());
        }

        eprintln!(
            "[{}] started node_id={} iface={} heartbeat_hz={} rpc_group=0x{:08x}:{}",
            self.options.name,
            self.options.node_id,
            self.options.iface_address,
            self.options.heartbeat_rate_hz,
            endpoint_ip,
            endpoint_port
        );

        Ok(())
    }

    fn shutdown(&mut self) {
        if self.node.is_null() {
            return;
        }
        unsafe {
            let _ = go_demo_node_shutdown(self.node);
            go_demo_node_destroy(self.node);
        }
        self.node = ptr::null_mut();
        eprintln!("[{}] stopped", self.options.name);
    }

    fn initialize_registers(&mut self) {
        self.registers.clear();
        self.registers.push(RegisterEntry {
            name: "uavcan.node.id".to_string(),
            kind: RegisterKind::Natural16,
            mutable_: true,
            persistent: true,
            natural16: self.options.node_id,
            natural32: 0,
            string_value: String::new(),
        });
        self.registers.push(RegisterEntry {
            name: "uavcan.node.description".to_string(),
            kind: RegisterKind::String,
            mutable_: true,
            persistent: true,
            natural16: 0,
            natural32: 0,
            string_value: "llvm-dsdl native register demo node".to_string(),
        });
        self.registers.push(RegisterEntry {
            name: "uavcan.udp.iface".to_string(),
            kind: RegisterKind::String,
            mutable_: true,
            persistent: true,
            natural16: 0,
            natural32: 0,
            string_value: self.options.iface_address.clone(),
        });
        self.registers.push(RegisterEntry {
            name: "demo.rate_hz".to_string(),
            kind: RegisterKind::Natural32,
            mutable_: true,
            persistent: true,
            natural16: 0,
            natural32: self.options.heartbeat_rate_hz,
            string_value: String::new(),
        });
        self.registers.push(RegisterEntry {
            name: "demo.counter".to_string(),
            kind: RegisterKind::Natural32,
            mutable_: true,
            persistent: false,
            natural16: 0,
            natural32: 0,
            string_value: String::new(),
        });
        self.registers.push(RegisterEntry {
            name: "sys.version".to_string(),
            kind: RegisterKind::String,
            mutable_: false,
            persistent: true,
            natural16: 0,
            natural32: 0,
            string_value: "0.1.0-demo".to_string(),
        });
    }

    fn update_heartbeat_period_from_registers(&mut self) {
        let rate = self.find_register("demo.rate_hz");
        if let Some(entry) = rate {
            if entry.kind == RegisterKind::Natural32 {
                let hz = if entry.natural32 == 0 {
                    1
                } else {
                    entry.natural32
                };
                self.heartbeat_period_usec = cmp::max(1, 1_000_000u64 / (hz as u64));
                return;
            }
        }
        self.heartbeat_period_usec = 1_000_000;
    }

    fn find_register(&self, name: &str) -> Option<&RegisterEntry> {
        self.registers.iter().find(|entry| entry.name == name)
    }

    fn find_register_mut(&mut self, name: &str) -> Option<&mut RegisterEntry> {
        self.registers.iter_mut().find(|entry| entry.name == name)
    }

    fn publish_heartbeat(&mut self, now_usec: u64) -> Result<(), String> {
        let current_counter = self.heartbeat_counter;
        if let Some(counter) = self.find_register_mut("demo.counter") {
            counter.natural32 = current_counter;
        }
        self.heartbeat_counter = self.heartbeat_counter.wrapping_add(1);

        let uptime_seconds = if now_usec > self.started_at_usec {
            (now_usec - self.started_at_usec) / 1_000_000
        } else {
            0
        };

        let mut heartbeat = uavcan_node_Heartbeat_1_0::default();
        heartbeat.uptime = cmp::min(uptime_seconds, u32::MAX as u64) as u32;
        heartbeat.health = uavcan_node_Health_1_0 {
            value: uavcan_node_Health_1_0::NOMINAL as u8,
        };
        heartbeat.mode = uavcan_node_Mode_1_0 {
            value: uavcan_node_Mode_1_0::OPERATIONAL as u8,
        };
        heartbeat.vendor_specific_status_code = (self.heartbeat_counter & 0xFF) as u8;

        let mut encoded = vec![0u8; uavcan_node_Heartbeat_1_0::SERIALIZATION_BUFFER_SIZE_BYTES];
        let used = heartbeat
            .serialize(&mut encoded)
            .map_err(|rc| format!("heartbeat serialisation failed: {}", rc))?;
        encoded.truncate(used);

        let deadline = now_usec.saturating_add(TX_DEADLINE_USEC);
        let rc = unsafe {
            go_demo_node_publish(
                self.node,
                SUBJECT_HEARTBEAT,
                self.heartbeat_transfer_id,
                encoded.as_ptr(),
                encoded.len(),
                deadline,
                go_demo_priority_nominal(),
            )
        };
        if rc < 0 {
            return Err(format!("go_demo_node_publish failed: {}", rc));
        }

        self.heartbeat_transfer_id = self.heartbeat_transfer_id.wrapping_add(1);
        Ok(())
    }

    fn send_rpc_response(
        &mut self,
        service_id: u16,
        destination_node_id: u16,
        transfer_id: u8,
        payload: &[u8],
    ) -> Result<(), String> {
        let deadline = unsafe { go_demo_now_usec() }.saturating_add(TX_DEADLINE_USEC);
        let payload_ptr = if payload.is_empty() {
            ptr::null()
        } else {
            payload.as_ptr()
        };
        let rc = unsafe {
            go_demo_node_respond(
                self.node,
                service_id,
                destination_node_id,
                transfer_id,
                payload_ptr,
                payload.len(),
                deadline,
                go_demo_priority_nominal(),
            )
        };
        if rc < 0 {
            return Err(format!("go_demo_node_respond failed: {}", rc));
        }
        Ok(())
    }

    fn handle_register_list_request(
        &mut self,
        source_node_id: u16,
        transfer_id: u8,
        payload: &[u8],
    ) -> Result<(), String> {
        let mut request = uavcan_register_List_1_0_Request::default();
        request
            .deserialize(payload)
            .map_err(|rc| format!("failed to deserialize register.List request: {}", rc))?;

        let mut response = uavcan_register_List_1_0_Response::default();
        if (request.index as usize) < self.registers.len() {
            response.name = uavcan_register_Name_1_0 {
                name: self.registers[request.index as usize]
                    .name
                    .as_bytes()
                    .to_vec(),
            };
        } else {
            response.name = uavcan_register_Name_1_0 { name: Vec::new() };
        }

        let mut encoded =
            vec![0u8; uavcan_register_List_1_0_Response::SERIALIZATION_BUFFER_SIZE_BYTES];
        let used = response
            .serialize(&mut encoded)
            .map_err(|rc| format!("failed to serialize register.List response: {}", rc))?;
        encoded.truncate(used);

        self.send_rpc_response(SERVICE_REGISTER_LIST, source_node_id, transfer_id, &encoded)
    }

    fn handle_register_access_request(
        &mut self,
        source_node_id: u16,
        transfer_id: u8,
        payload: &[u8],
    ) -> Result<(), String> {
        let mut request = uavcan_register_Access_1_0_Request::default();
        request
            .deserialize(payload)
            .map_err(|rc| format!("failed to deserialize register.Access request: {}", rc))?;

        let requested_name = String::from_utf8_lossy(&request.name.name).to_string();
        let mut response = uavcan_register_Access_1_0_Response::default();
        response.timestamp = uavcan_time_SynchronizedTimestamp_1_0 {
            microsecond: uavcan_time_SynchronizedTimestamp_1_0::UNKNOWN,
        };

        let mut refresh_heartbeat_period = false;
        if let Some(entry) = self.find_register_mut(&requested_name) {
            if request.value._tag_ != VALUE_TAG_EMPTY && entry.mutable_ {
                let _ = apply_register_write(entry, &request.value);
                if entry.name == "demo.rate_hz" {
                    refresh_heartbeat_period = true;
                }
            }
            response.mutable = entry.mutable_;
            response.persistent = entry.persistent;
            response.value = export_register_value(entry);
        } else {
            response.mutable = false;
            response.persistent = false;
            response.value = make_empty_value();
        }
        if refresh_heartbeat_period {
            self.update_heartbeat_period_from_registers();
        }

        let mut encoded =
            vec![0u8; uavcan_register_Access_1_0_Response::SERIALIZATION_BUFFER_SIZE_BYTES];
        let used = response
            .serialize(&mut encoded)
            .map_err(|rc| format!("failed to serialize register.Access response: {}", rc))?;
        encoded.truncate(used);

        self.send_rpc_response(
            SERVICE_REGISTER_ACCESS,
            source_node_id,
            transfer_id,
            &encoded,
        )
    }

    fn run(&mut self, stop_requested: &AtomicBool) -> Result<(), String> {
        loop {
            if stop_requested.load(Ordering::Relaxed) {
                return Ok(());
            }

            let now = unsafe { go_demo_now_usec() };
            if now >= self.next_heartbeat_at {
                self.publish_heartbeat(now)?;
                while self.next_heartbeat_at <= now {
                    self.next_heartbeat_at = self
                        .next_heartbeat_at
                        .saturating_add(self.heartbeat_period_usec);
                }
            }

            unsafe {
                go_demo_node_pump_tx(self.node);
            }

            let now_after_tx = unsafe { go_demo_now_usec() };
            let until_next_heartbeat = if self.next_heartbeat_at > now_after_tx {
                self.next_heartbeat_at - now_after_tx
            } else {
                self.heartbeat_period_usec
            };
            let timeout_usec = cmp::max(1, cmp::min(until_next_heartbeat, MAX_WAIT_USEC));

            let mut transfer = GoDemoRpcTransfer::default();
            let poll_rc = unsafe {
                go_demo_node_poll_rpc(
                    self.node,
                    timeout_usec,
                    &mut transfer as *mut GoDemoRpcTransfer,
                )
            };
            if poll_rc < 0 {
                if poll_rc == -4 {
                    continue;
                }
                return Err(format!("go_demo_node_poll_rpc failed: {}", poll_rc));
            }
            if poll_rc == 0 {
                continue;
            }

            let payload = if transfer.payload_size == 0 {
                Vec::new()
            } else {
                unsafe {
                    std::slice::from_raw_parts(transfer.payload as *const u8, transfer.payload_size)
                }
                .to_vec()
            };
            let service_id = transfer.service_id;
            let source_node_id = transfer.source_node_id;
            let transfer_id = transfer.transfer_id;

            unsafe {
                go_demo_node_release_transfer(&mut transfer as *mut GoDemoRpcTransfer);
            }

            let result = match service_id {
                SERVICE_REGISTER_LIST => {
                    self.handle_register_list_request(source_node_id, transfer_id, &payload)
                }
                SERVICE_REGISTER_ACCESS => {
                    self.handle_register_access_request(source_node_id, transfer_id, &payload)
                }
                _ => Ok(()),
            };
            if let Err(err) = result {
                eprintln!("[{}] rpc handling failed: {}", self.options.name, err);
            }
        }
    }
}

impl Drop for NodeApp {
    fn drop(&mut self) {
        self.shutdown();
    }
}

fn make_natural16_value(value: u16) -> uavcan_register_Value_1_0 {
    let mut out = uavcan_register_Value_1_0::default();
    out._tag_ = VALUE_TAG_NATURAL16;
    out.natural16 = uavcan_primitive_array_Natural16_1_0 { value: vec![value] };
    out
}

fn make_natural32_value(value: u32) -> uavcan_register_Value_1_0 {
    let mut out = uavcan_register_Value_1_0::default();
    out._tag_ = VALUE_TAG_NATURAL32;
    out.natural32 = uavcan_primitive_array_Natural32_1_0 { value: vec![value] };
    out
}

fn make_string_value(value: &str) -> uavcan_register_Value_1_0 {
    let mut out = uavcan_register_Value_1_0::default();
    out._tag_ = VALUE_TAG_STRING;
    out.string = uavcan_primitive_String_1_0 {
        value: value.as_bytes().to_vec(),
    };
    out
}

fn make_empty_value() -> uavcan_register_Value_1_0 {
    let mut out = uavcan_register_Value_1_0::default();
    out._tag_ = VALUE_TAG_EMPTY;
    out
}

fn export_register_value(entry: &RegisterEntry) -> uavcan_register_Value_1_0 {
    match entry.kind {
        RegisterKind::Natural16 => make_natural16_value(entry.natural16),
        RegisterKind::Natural32 => make_natural32_value(entry.natural32),
        RegisterKind::String => make_string_value(&entry.string_value),
    }
}

fn extract_single_unsigned(value: &uavcan_register_Value_1_0) -> Option<u64> {
    match value._tag_ {
        VALUE_TAG_NATURAL8 => value.natural8.value.first().map(|v| *v as u64),
        VALUE_TAG_NATURAL16 => value.natural16.value.first().map(|v| *v as u64),
        VALUE_TAG_NATURAL32 => value.natural32.value.first().map(|v| *v as u64),
        VALUE_TAG_NATURAL64 => value.natural64.value.first().copied(),
        VALUE_TAG_INTEGER8 => {
            value
                .integer8
                .value
                .first()
                .and_then(|v| if *v >= 0 { Some(*v as u64) } else { None })
        }
        VALUE_TAG_INTEGER16 => {
            value
                .integer16
                .value
                .first()
                .and_then(|v| if *v >= 0 { Some(*v as u64) } else { None })
        }
        VALUE_TAG_INTEGER32 => {
            value
                .integer32
                .value
                .first()
                .and_then(|v| if *v >= 0 { Some(*v as u64) } else { None })
        }
        VALUE_TAG_INTEGER64 => {
            value
                .integer64
                .value
                .first()
                .and_then(|v| if *v >= 0 { Some(*v as u64) } else { None })
        }
        _ => None,
    }
}

fn apply_register_write(entry: &mut RegisterEntry, value: &uavcan_register_Value_1_0) -> bool {
    if value._tag_ == VALUE_TAG_EMPTY {
        return false;
    }

    match entry.kind {
        RegisterKind::Natural16 => {
            if let Some(parsed) = extract_single_unsigned(value) {
                entry.natural16 = cmp::min(parsed, u16::MAX as u64) as u16;
                return true;
            }
            false
        }
        RegisterKind::Natural32 => {
            if let Some(parsed) = extract_single_unsigned(value) {
                entry.natural32 = cmp::min(parsed, u32::MAX as u64) as u32;
                return true;
            }
            false
        }
        RegisterKind::String => {
            if value._tag_ != VALUE_TAG_STRING {
                return false;
            }
            entry.string_value = String::from_utf8_lossy(&value.string.value).to_string();
            true
        }
    }
}

fn print_usage(program_name: &str) {
    let node_id_max = unsafe { go_demo_node_id_max() };
    eprintln!(
        "Usage: {} [options]\n  --name <label>              Node label for log output (default: rust)\n  --node-id <n>               Local node-ID [0, {}]\n  --iface <ipv4>              Local iface IPv4 address (default: 127.0.0.1)\n  --heartbeat-rate-hz <n>     Heartbeat publication rate in Hz (default: 1)\n  --help                      Show this help",
        program_name, node_id_max
    );
}

fn parse_u64(text: &str) -> Option<u64> {
    text.parse::<u64>().ok()
}

fn parse_options(args: &[String]) -> ParseResult {
    let mut options = Options {
        name: "rust".to_string(),
        node_id: 0,
        iface_address: "127.0.0.1".to_string(),
        heartbeat_rate_hz: 1,
    };

    let mut node_id_seen = false;
    let mut idx = 1usize;
    while idx < args.len() {
        let arg = &args[idx];
        if arg == "--help" {
            print_usage(&args[0]);
            return ParseResult::Help;
        }
        if idx + 1 >= args.len() {
            eprintln!("Missing value for option: {}", arg);
            return ParseResult::Error;
        }

        let value = &args[idx + 1];
        match arg.as_str() {
            "--name" => {
                options.name = value.clone();
            }
            "--node-id" => {
                let parsed = match parse_u64(value) {
                    Some(v) => v,
                    None => {
                        eprintln!("Invalid --node-id: {}", value);
                        return ParseResult::Error;
                    }
                };
                let node_id_max = unsafe { go_demo_node_id_max() } as u64;
                if parsed > node_id_max {
                    eprintln!("Invalid --node-id: {}", value);
                    return ParseResult::Error;
                }
                options.node_id = parsed as u16;
                node_id_seen = true;
            }
            "--iface" => {
                options.iface_address = value.clone();
            }
            "--heartbeat-rate-hz" => {
                let parsed = match parse_u64(value) {
                    Some(v) => v,
                    None => {
                        eprintln!("Invalid --heartbeat-rate-hz: {}", value);
                        return ParseResult::Error;
                    }
                };
                if parsed == 0 || parsed > 1000 {
                    eprintln!("Invalid --heartbeat-rate-hz: {}", value);
                    return ParseResult::Error;
                }
                options.heartbeat_rate_hz = parsed as u32;
            }
            _ => {
                eprintln!("Unknown option: {}", arg);
                return ParseResult::Error;
            }
        }

        idx += 2;
    }

    if !node_id_seen {
        eprintln!("--node-id is required");
        return ParseResult::Error;
    }

    ParseResult::Success(options)
}

fn main() {
    let args: Vec<String> = env::args().collect();

    let options = match parse_options(&args) {
        ParseResult::Success(options) => options,
        ParseResult::Help => return,
        ParseResult::Error => {
            print_usage(
                args.get(0)
                    .map_or("cyphal-yakut-register-rust-node", String::as_str),
            );
            std::process::exit(1);
        }
    };

    let mut app = NodeApp::new(options);
    if let Err(err) = app.initialize() {
        eprintln!("[{}] {}", app.options.name, err);
        std::process::exit(1);
    }

    let stop_requested = Arc::new(AtomicBool::new(false));
    let stop_requested_handler = Arc::clone(&stop_requested);
    if let Err(err) = ctrlc::set_handler(move || {
        stop_requested_handler.store(true, Ordering::Relaxed);
    }) {
        eprintln!(
            "[{}] failed to install signal handler: {}",
            app.options.name, err
        );
        std::process::exit(1);
    }

    if let Err(err) = app.run(&stop_requested) {
        eprintln!("[{}] {}", app.options.name, err);
        std::process::exit(1);
    }
}
