cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC FIXTURES_ROOT OUT_DIR CARGO_EXECUTABLE)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT DEFINED RUST_PROFILE OR "${RUST_PROFILE}" STREQUAL "")
  set(RUST_PROFILE "std")
endif()
if(NOT DEFINED RUST_RUNTIME_SPECIALIZATION OR
   "${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "")
  set(RUST_RUNTIME_SPECIALIZATION "portable")
endif()
if(NOT DEFINED RUST_MEMORY_MODE OR "${RUST_MEMORY_MODE}" STREQUAL "")
  set(RUST_MEMORY_MODE "max-inline")
endif()
if(NOT DEFINED RUST_INLINE_THRESHOLD_BYTES OR
   "${RUST_INLINE_THRESHOLD_BYTES}" STREQUAL "")
  set(RUST_INLINE_THRESHOLD_BYTES "256")
endif()

if(NOT "${RUST_PROFILE}" STREQUAL "std" AND
   NOT "${RUST_PROFILE}" STREQUAL "no-std-alloc")
  message(FATAL_ERROR "Invalid RUST_PROFILE value: ${RUST_PROFILE}")
endif()
if(NOT "${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "portable" AND
   NOT "${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
  message(FATAL_ERROR
    "Invalid RUST_RUNTIME_SPECIALIZATION value: ${RUST_RUNTIME_SPECIALIZATION}")
endif()
if(NOT "${RUST_MEMORY_MODE}" STREQUAL "max-inline" AND
   NOT "${RUST_MEMORY_MODE}" STREQUAL "inline-then-pool")
  message(FATAL_ERROR "Invalid RUST_MEMORY_MODE value: ${RUST_MEMORY_MODE}")
endif()
if(NOT RUST_INLINE_THRESHOLD_BYTES MATCHES "^[0-9]+$")
  message(FATAL_ERROR
    "Invalid RUST_INLINE_THRESHOLD_BYTES value: ${RUST_INLINE_THRESHOLD_BYTES}")
endif()
if(RUST_INLINE_THRESHOLD_BYTES LESS 1)
  message(FATAL_ERROR
    "RUST_INLINE_THRESHOLD_BYTES must be positive: ${RUST_INLINE_THRESHOLD_BYTES}")
endif()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${FIXTURES_ROOT}")
  message(FATAL_ERROR "fixtures root not found: ${FIXTURES_ROOT}")
endif()
if(NOT EXISTS "${CARGO_EXECUTABLE}")
  message(FATAL_ERROR "cargo executable not found: ${CARGO_EXECUTABLE}")
endif()

set(vendor_root "${FIXTURES_ROOT}/vendor")
if(NOT EXISTS "${vendor_root}")
  message(FATAL_ERROR "vendor fixtures root not found: ${vendor_root}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language rust --experimental-languages
      "${vendor_root}"
      --outdir "${OUT_DIR}"
      --rust-crate-name "llvmdsdl_runtime_unit"
      --rust-profile "${RUST_PROFILE}"
      --rust-runtime-specialization "${RUST_RUNTIME_SPECIALIZATION}"
      --rust-memory-mode "${RUST_MEMORY_MODE}"
      --rust-inline-threshold-bytes "${RUST_INLINE_THRESHOLD_BYTES}"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "Rust runtime unit fixture generation failed")
endif()

foreach(required
    "${OUT_DIR}/Cargo.toml"
    "${OUT_DIR}/src/lib.rs"
    "${OUT_DIR}/src/dsdl_runtime.rs"
    "${OUT_DIR}/src/dsdl_runtime_semantic_wrappers.rs")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Missing required generated file: ${required}")
  endif()
endforeach()

set(target_dir "${OUT_DIR}/cargo-target")
set(cargo_args test --quiet --lib --manifest-path "${OUT_DIR}/Cargo.toml")
if("${RUST_PROFILE}" STREQUAL "no-std-alloc")
  list(APPEND cargo_args --no-default-features)
  if("${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
    list(APPEND cargo_args --features "std,runtime-fast")
  else()
    list(APPEND cargo_args --features "std")
  endif()
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "CARGO_TARGET_DIR=${target_dir}"
      "${CARGO_EXECUTABLE}" ${cargo_args}
  RESULT_VARIABLE test_result
  OUTPUT_VARIABLE test_stdout
  ERROR_VARIABLE test_stderr
)
if(NOT test_result EQUAL 0)
  message(STATUS "cargo test stdout:\n${test_stdout}")
  message(STATUS "cargo test stderr:\n${test_stderr}")
  message(FATAL_ERROR "Generated Rust runtime unit tests failed")
endif()

file(WRITE "${OUT_DIR}/rust-runtime-unit-summary.txt" "${test_stdout}\n${test_stderr}\n")
message(STATUS
  "Rust runtime unit tests passed (${RUST_PROFILE}, ${RUST_RUNTIME_SPECIALIZATION}, ${RUST_MEMORY_MODE})")
