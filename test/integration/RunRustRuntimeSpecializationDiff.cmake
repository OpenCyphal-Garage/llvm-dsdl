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

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()

if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "uavcan root not found: ${UAVCAN_ROOT}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(portable_out "${OUT_DIR}/rust-portable")
set(fast_out "${OUT_DIR}/rust-fast")

execute_process(
  COMMAND
    "${DSDLC}" --target-language rust --experimental-languages
      "${UAVCAN_ROOT}"
      --outdir "${portable_out}"
      --rust-crate-name "uavcan_dsdl_generated"
      --rust-profile "${RUST_PROFILE}"
      --rust-runtime-specialization "portable"
  RESULT_VARIABLE portable_result
  OUTPUT_VARIABLE portable_stdout
  ERROR_VARIABLE portable_stderr
)
if(NOT portable_result EQUAL 0)
  message(STATUS "portable generation stdout:\n${portable_stdout}")
  message(STATUS "portable generation stderr:\n${portable_stderr}")
  message(FATAL_ERROR "failed to generate Rust portable runtime profile output")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language rust --experimental-languages
      "${UAVCAN_ROOT}"
      --outdir "${fast_out}"
      --rust-crate-name "uavcan_dsdl_generated"
      --rust-profile "${RUST_PROFILE}"
      --rust-runtime-specialization "fast"
  RESULT_VARIABLE fast_result
  OUTPUT_VARIABLE fast_stdout
  ERROR_VARIABLE fast_stderr
)
if(NOT fast_result EQUAL 0)
  message(STATUS "fast generation stdout:\n${fast_stdout}")
  message(STATUS "fast generation stderr:\n${fast_stderr}")
  message(FATAL_ERROR "failed to generate Rust fast runtime profile output")
endif()

foreach(required
    "${portable_out}/Cargo.toml"
    "${portable_out}/src/lib.rs"
    "${portable_out}/src/dsdl_runtime.rs"
    "${portable_out}/src/dsdl_runtime_semantic_wrappers.rs"
    "${fast_out}/Cargo.toml"
    "${fast_out}/src/lib.rs"
    "${fast_out}/src/dsdl_runtime.rs"
    "${fast_out}/src/dsdl_runtime_semantic_wrappers.rs")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Missing required generated file: ${required}")
  endif()
endforeach()

file(READ "${portable_out}/Cargo.toml" portable_cargo_toml)
file(READ "${fast_out}/Cargo.toml" fast_cargo_toml)
if("${RUST_PROFILE}" STREQUAL "no-std-alloc")
  if(NOT portable_cargo_toml MATCHES "default = \\[\\]")
    message(FATAL_ERROR
      "Expected no-std portable Cargo.toml default features to be empty")
  endif()
  if(NOT fast_cargo_toml MATCHES "default = \\[\"runtime-fast\"\\]")
    message(FATAL_ERROR
      "Expected no-std fast Cargo.toml default features to include runtime-fast")
  endif()
else()
  if(NOT portable_cargo_toml MATCHES "default = \\[\"std\"\\]")
    message(FATAL_ERROR
      "Expected std portable Cargo.toml default features to include std")
  endif()
  if(NOT fast_cargo_toml MATCHES "default = \\[\"std\", \"runtime-fast\"\\]")
    message(FATAL_ERROR
      "Expected std fast Cargo.toml default features to include std and runtime-fast")
  endif()
endif()
if(NOT fast_cargo_toml MATCHES "runtime-fast = \\[\\]")
  message(FATAL_ERROR "Expected Cargo.toml to declare runtime-fast feature")
endif()

set(portable_src "${portable_out}/src")
set(fast_src "${fast_out}/src")

file(GLOB_RECURSE portable_rs "${portable_src}/*.rs")
file(GLOB_RECURSE fast_rs "${fast_src}/*.rs")

set(portable_semantic_files "")
foreach(path IN LISTS portable_rs)
  file(RELATIVE_PATH rel "${portable_src}" "${path}")
  if(rel STREQUAL "lib.rs" OR rel STREQUAL "dsdl_runtime.rs" OR rel STREQUAL "dsdl_runtime_semantic_wrappers.rs")
    continue()
  endif()
  list(APPEND portable_semantic_files "${rel}")
endforeach()

set(fast_semantic_files "")
foreach(path IN LISTS fast_rs)
  file(RELATIVE_PATH rel "${fast_src}" "${path}")
  if(rel STREQUAL "lib.rs" OR rel STREQUAL "dsdl_runtime.rs" OR rel STREQUAL "dsdl_runtime_semantic_wrappers.rs")
    continue()
  endif()
  list(APPEND fast_semantic_files "${rel}")
endforeach()

list(SORT portable_semantic_files)
list(SORT fast_semantic_files)

if(NOT portable_semantic_files STREQUAL fast_semantic_files)
  message(FATAL_ERROR
    "Rust runtime specialization semantic file inventory mismatch between portable and fast outputs")
endif()

foreach(rel IN LISTS portable_semantic_files)
  file(READ "${portable_src}/${rel}" portable_text)
  file(READ "${fast_src}/${rel}" fast_text)
  if(NOT portable_text STREQUAL fast_text)
    message(FATAL_ERROR
      "Rust runtime specialization semantic mismatch in generated file: ${rel}")
  endif()
endforeach()

message(STATUS
  "Rust runtime specialization diff passed (${RUST_PROFILE}): ${portable_src} and ${fast_src} semantic files are identical")
