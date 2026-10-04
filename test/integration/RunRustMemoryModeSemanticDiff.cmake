cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT DEFINED RUST_PROFILE OR "${RUST_PROFILE}" STREQUAL "")
  set(RUST_PROFILE "std")
endif()
if(NOT "${RUST_PROFILE}" STREQUAL "std" AND
   NOT "${RUST_PROFILE}" STREQUAL "no-std-alloc")
  message(FATAL_ERROR "Invalid RUST_PROFILE value: ${RUST_PROFILE}")
endif()
if(NOT DEFINED RUST_RUNTIME_SPECIALIZATION OR
   "${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "")
  set(RUST_RUNTIME_SPECIALIZATION "portable")
endif()
if(NOT "${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "portable" AND
   NOT "${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
  message(FATAL_ERROR
    "Invalid RUST_RUNTIME_SPECIALIZATION value: ${RUST_RUNTIME_SPECIALIZATION}")
endif()
if(NOT DEFINED RUST_INLINE_THRESHOLD_BYTES OR
   "${RUST_INLINE_THRESHOLD_BYTES}" STREQUAL "")
  set(RUST_INLINE_THRESHOLD_BYTES "256")
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
if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "uavcan root not found: ${UAVCAN_ROOT}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(max_inline_out "${OUT_DIR}/rust-max-inline")
set(inline_pool_out "${OUT_DIR}/rust-inline-then-pool")

execute_process(
  COMMAND
    "${DSDLC}" --target-language rust --experimental-languages
      "${UAVCAN_ROOT}"
      --outdir "${max_inline_out}"
      --rust-crate-name "uavcan_dsdl_generated"
      --rust-profile "${RUST_PROFILE}"
      --rust-runtime-specialization "${RUST_RUNTIME_SPECIALIZATION}"
      --rust-memory-mode "max-inline"
      --rust-inline-threshold-bytes "${RUST_INLINE_THRESHOLD_BYTES}"
  RESULT_VARIABLE max_inline_result
  OUTPUT_VARIABLE max_inline_stdout
  ERROR_VARIABLE max_inline_stderr
)
if(NOT max_inline_result EQUAL 0)
  message(STATUS "max-inline generation stdout:\n${max_inline_stdout}")
  message(STATUS "max-inline generation stderr:\n${max_inline_stderr}")
  message(FATAL_ERROR "failed to generate Rust max-inline output")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language rust --experimental-languages
      "${UAVCAN_ROOT}"
      --outdir "${inline_pool_out}"
      --rust-crate-name "uavcan_dsdl_generated"
      --rust-profile "${RUST_PROFILE}"
      --rust-runtime-specialization "${RUST_RUNTIME_SPECIALIZATION}"
      --rust-memory-mode "inline-then-pool"
      --rust-inline-threshold-bytes "${RUST_INLINE_THRESHOLD_BYTES}"
  RESULT_VARIABLE inline_pool_result
  OUTPUT_VARIABLE inline_pool_stdout
  ERROR_VARIABLE inline_pool_stderr
)
if(NOT inline_pool_result EQUAL 0)
  message(STATUS "inline-then-pool generation stdout:\n${inline_pool_stdout}")
  message(STATUS "inline-then-pool generation stderr:\n${inline_pool_stderr}")
  message(FATAL_ERROR "failed to generate Rust inline-then-pool output")
endif()

foreach(required
    "${max_inline_out}/Cargo.toml"
    "${max_inline_out}/src/lib.rs"
    "${max_inline_out}/src/dsdl_runtime.rs"
    "${max_inline_out}/src/dsdl_runtime_semantic_wrappers.rs"
    "${inline_pool_out}/Cargo.toml"
    "${inline_pool_out}/src/lib.rs"
    "${inline_pool_out}/src/dsdl_runtime.rs"
    "${inline_pool_out}/src/dsdl_runtime_semantic_wrappers.rs")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Missing required generated file: ${required}")
  endif()
endforeach()

file(READ "${max_inline_out}/Cargo.toml" max_inline_cargo_toml)
file(READ "${inline_pool_out}/Cargo.toml" inline_pool_cargo_toml)
if(NOT max_inline_cargo_toml MATCHES "rust-memory-mode = \"max-inline\"")
  message(FATAL_ERROR "Expected Cargo.toml to record rust-memory-mode max-inline")
endif()
if(NOT inline_pool_cargo_toml MATCHES "rust-memory-mode = \"inline-then-pool\"")
  message(FATAL_ERROR
    "Expected Cargo.toml to record rust-memory-mode inline-then-pool")
endif()
if(NOT max_inline_cargo_toml MATCHES "rust-inline-threshold-bytes = ${RUST_INLINE_THRESHOLD_BYTES}")
  message(FATAL_ERROR
    "Expected max-inline Cargo.toml rust-inline-threshold-bytes metadata")
endif()
if(NOT inline_pool_cargo_toml MATCHES "rust-inline-threshold-bytes = ${RUST_INLINE_THRESHOLD_BYTES}")
  message(FATAL_ERROR
    "Expected inline-then-pool Cargo.toml rust-inline-threshold-bytes metadata")
endif()

set(max_inline_src "${max_inline_out}/src")
set(inline_pool_src "${inline_pool_out}/src")

file(GLOB_RECURSE max_inline_rs "${max_inline_src}/*.rs")
file(GLOB_RECURSE inline_pool_rs "${inline_pool_src}/*.rs")

set(max_inline_semantic_files "")
foreach(path IN LISTS max_inline_rs)
  file(RELATIVE_PATH rel "${max_inline_src}" "${path}")
  if(rel STREQUAL "lib.rs" OR rel STREQUAL "dsdl_runtime.rs" OR rel STREQUAL "dsdl_runtime_semantic_wrappers.rs")
    continue()
  endif()
  list(APPEND max_inline_semantic_files "${rel}")
endforeach()

set(inline_pool_semantic_files "")
foreach(path IN LISTS inline_pool_rs)
  file(RELATIVE_PATH rel "${inline_pool_src}" "${path}")
  if(rel STREQUAL "lib.rs" OR rel STREQUAL "dsdl_runtime.rs" OR rel STREQUAL "dsdl_runtime_semantic_wrappers.rs")
    continue()
  endif()
  list(APPEND inline_pool_semantic_files "${rel}")
endforeach()

list(SORT max_inline_semantic_files)
list(SORT inline_pool_semantic_files)

if(NOT max_inline_semantic_files STREQUAL inline_pool_semantic_files)
  message(FATAL_ERROR
    "Rust memory-mode semantic file inventory mismatch between max-inline and inline-then-pool outputs")
endif()

foreach(rel IN LISTS max_inline_semantic_files)
  file(READ "${max_inline_src}/${rel}" max_inline_text)
  file(READ "${inline_pool_src}/${rel}" inline_pool_text)

  string(REGEX REPLACE
    "pub const __LLVMDSDL_MEMORY_MODE: crate::dsdl_runtime::DsdlMemoryMode = [^;]+;"
    "pub const __LLVMDSDL_MEMORY_MODE: crate::dsdl_runtime::DsdlMemoryMode = <normalized>;"
    max_inline_normalized
    "${max_inline_text}")
  string(REGEX REPLACE
    "pub const __LLVMDSDL_MEMORY_MODE: crate::dsdl_runtime::DsdlMemoryMode = [^;]+;"
    "pub const __LLVMDSDL_MEMORY_MODE: crate::dsdl_runtime::DsdlMemoryMode = <normalized>;"
    inline_pool_normalized
    "${inline_pool_text}")

  if(NOT max_inline_normalized STREQUAL inline_pool_normalized)
    message(FATAL_ERROR
      "Rust memory-mode semantic mismatch in generated file: ${rel}")
  endif()
endforeach()

message(STATUS
  "Rust memory-mode semantic diff passed (${RUST_PROFILE}, ${RUST_RUNTIME_SPECIALIZATION}): "
  "${max_inline_src} and ${inline_pool_src} differ only in expected memory-mode constants")
