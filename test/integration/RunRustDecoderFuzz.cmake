# Robustness fuzz lane over the generated Rust deserializers.
#
# Generates the Rust crate for the UAVCAN corpus and builds a small binary
# (test/integration/RustDecoderFuzzMain.rs) that feeds a deterministic stream of
# pseudo-random byte slices into each generated `deserialize` for the adversarial
# type set, wrapping each call in catch_unwind. Rust is memory-safe, so the
# caught class is a PANIC on attacker-controlled bytes (out-of-range slice), a
# real robustness / DoS defect. Iteration count is overridable via the env var
# LLVMDSDL_RUST_FUZZ_ITERS for the deep / cron run.
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR CARGO_EXECUTABLE SOURCE_ROOT)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "uavcan root not found: ${UAVCAN_ROOT}")
endif()
if(NOT EXISTS "${CARGO_EXECUTABLE}")
  message(FATAL_ERROR "cargo executable not found: ${CARGO_EXECUTABLE}")
endif()

set(cargo_toml_template "${SOURCE_ROOT}/test/integration/RustDecoderFuzzCargo.toml.in")
set(main_rs_template "${SOURCE_ROOT}/test/integration/RustDecoderFuzzMain.rs")
foreach(path "${cargo_toml_template}" "${main_rs_template}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Rust decoder fuzz input missing: ${path}")
  endif()
endforeach()

execute_process(
  COMMAND "${DSDLC}" --version
  OUTPUT_VARIABLE dsdlc_version_stdout
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX REPLACE "^dsdlc[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)$" "\\1" LLVMDSDL_TOOL_VERSION "${dsdlc_version_stdout}")
if(NOT LLVMDSDL_TOOL_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  set(LLVMDSDL_TOOL_VERSION "0.0.0")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
set(rust_out "${OUT_DIR}/rust")
set(harness_out "${OUT_DIR}/harness")
file(MAKE_DIRECTORY "${rust_out}")
file(MAKE_DIRECTORY "${harness_out}/src")

execute_process(
  COMMAND
    "${DSDLC}" --target-language rust --versioned-type-names
      "${UAVCAN_ROOT}"
      --rust-crate-name uavcan_dsdl_generated
      --outdir "${rust_out}"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "uavcan Rust generation failed")
endif()

set(RUST_OUT "${rust_out}")
if(NOT DEFINED C_TYPE_NAME_SCHEME)
  set(C_TYPE_NAME_SCHEME "unversioned")
endif()
if(NOT DEFINED TYPE_NAME_SCHEME)
  set(TYPE_NAME_SCHEME "versioned")
endif()
include("${SOURCE_ROOT}/cmake/HarnessTypeNameTokens.cmake")
llvmdsdl_harness_type_name_tokens("${C_TYPE_NAME_SCHEME}" "${TYPE_NAME_SCHEME}")

configure_file("${cargo_toml_template}" "${harness_out}/Cargo.toml" @ONLY)
configure_file("${main_rs_template}" "${harness_out}/src/main.rs" @ONLY)

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "CARGO_TARGET_DIR=${OUT_DIR}/cargo-target"
      "${CARGO_EXECUTABLE}" run --release --quiet --manifest-path "${harness_out}/Cargo.toml"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)
message(STATUS "cargo run stdout:\n${run_stdout}")
if(NOT run_stderr STREQUAL "")
  message(STATUS "cargo run stderr:\n${run_stderr}")
endif()
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "generated Rust decoder fuzz harness reported a failure (a decoder panicked on fuzzed input)")
endif()

message(STATUS "Rust decoder fuzz lane passed")
