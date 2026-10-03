cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR)
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

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(std_out "${OUT_DIR}/rust-std")
set(no_std_out "${OUT_DIR}/rust-no-std-alloc")

execute_process(
  COMMAND
    "${DSDLC}" --target-language rust --experimental-languages
      "${UAVCAN_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${std_out}"
      --rust-crate-name "uavcan_dsdl_generated"
      --rust-profile "std"
  RESULT_VARIABLE std_result
  OUTPUT_VARIABLE std_stdout
  ERROR_VARIABLE std_stderr
)
if(NOT std_result EQUAL 0)
  message(STATUS "std generation stdout:\n${std_stdout}")
  message(STATUS "std generation stderr:\n${std_stderr}")
  message(FATAL_ERROR "failed to generate Rust std profile output")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language rust --experimental-languages
      "${UAVCAN_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${no_std_out}"
      --rust-crate-name "uavcan_dsdl_generated"
      --rust-profile "no-std-alloc"
  RESULT_VARIABLE no_std_result
  OUTPUT_VARIABLE no_std_stdout
  ERROR_VARIABLE no_std_stderr
)
if(NOT no_std_result EQUAL 0)
  message(STATUS "no-std generation stdout:\n${no_std_stdout}")
  message(STATUS "no-std generation stderr:\n${no_std_stderr}")
  message(FATAL_ERROR "failed to generate Rust no-std profile output")
endif()

foreach(required
    "${std_out}/Cargo.toml"
    "${std_out}/src/lib.rs"
    "${std_out}/src/dsdl_runtime.rs"
    "${std_out}/src/dsdl_runtime_semantic_wrappers.rs"
    "${no_std_out}/Cargo.toml"
    "${no_std_out}/src/lib.rs"
    "${no_std_out}/src/dsdl_runtime.rs"
    "${no_std_out}/src/dsdl_runtime_semantic_wrappers.rs")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "missing required generated file: ${required}")
  endif()
endforeach()

file(READ "${std_out}/Cargo.toml" std_cargo_toml)
if(NOT std_cargo_toml MATCHES "default = \\[\"std\"\\]")
  message(FATAL_ERROR
    "expected std Cargo.toml to enable std in default features")
endif()

file(READ "${no_std_out}/Cargo.toml" no_std_cargo_toml)
if(NOT no_std_cargo_toml MATCHES "default = \\[\\]")
  message(FATAL_ERROR
    "expected no-std Cargo.toml to have empty default features")
endif()

set(std_src "${std_out}/src")
set(no_std_src "${no_std_out}/src")

file(GLOB_RECURSE std_rs "${std_src}/*.rs")
file(GLOB_RECURSE no_std_rs "${no_std_src}/*.rs")

set(std_semantic_files "")
foreach(path IN LISTS std_rs)
  file(RELATIVE_PATH rel "${std_src}" "${path}")
  if(rel STREQUAL "lib.rs" OR rel STREQUAL "dsdl_runtime.rs" OR rel STREQUAL "dsdl_runtime_semantic_wrappers.rs")
    continue()
  endif()
  list(APPEND std_semantic_files "${rel}")
endforeach()

set(no_std_semantic_files "")
foreach(path IN LISTS no_std_rs)
  file(RELATIVE_PATH rel "${no_std_src}" "${path}")
  if(rel STREQUAL "lib.rs" OR rel STREQUAL "dsdl_runtime.rs" OR rel STREQUAL "dsdl_runtime_semantic_wrappers.rs")
    continue()
  endif()
  list(APPEND no_std_semantic_files "${rel}")
endforeach()

list(SORT std_semantic_files)
list(SORT no_std_semantic_files)

if(NOT std_semantic_files STREQUAL no_std_semantic_files)
  message(FATAL_ERROR
    "Rust profile semantic file inventory mismatch between std and no-std outputs")
endif()

foreach(rel IN LISTS std_semantic_files)
  file(READ "${std_src}/${rel}" std_text)
  file(READ "${no_std_src}/${rel}" no_std_text)
  if(NOT std_text STREQUAL no_std_text)
    message(FATAL_ERROR
      "Rust profile semantic mismatch in generated file: ${rel}")
  endif()
endforeach()

message(STATUS
  "Rust profile semantic diff passed: ${std_src} and ${no_std_src} semantic files are identical")
