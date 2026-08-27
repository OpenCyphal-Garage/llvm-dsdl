cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR C_COMPILER AR_EXECUTABLE CARGO_EXECUTABLE SOURCE_ROOT)
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
if(NOT DEFINED RUST_MEMORY_MODE OR "${RUST_MEMORY_MODE}" STREQUAL "")
  set(RUST_MEMORY_MODE "max-inline")
endif()
if(NOT "${RUST_MEMORY_MODE}" STREQUAL "max-inline" AND
   NOT "${RUST_MEMORY_MODE}" STREQUAL "inline-then-pool")
  message(FATAL_ERROR "Invalid RUST_MEMORY_MODE value: ${RUST_MEMORY_MODE}")
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

include("${SOURCE_ROOT}/cmake/HarnessTypeNameTokens.cmake")
llvmdsdl_harness_naming_scheme(C_DEFAULT "unversioned" OTHER_DEFAULT "unversioned")

execute_process(
  COMMAND "${DSDLC}" --version
  RESULT_VARIABLE dsdlc_version_result
  OUTPUT_VARIABLE dsdlc_version_stdout
  ERROR_VARIABLE dsdlc_version_stderr
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT dsdlc_version_result EQUAL 0)
  message(STATUS "dsdlc --version stdout:\n${dsdlc_version_stdout}")
  message(STATUS "dsdlc --version stderr:\n${dsdlc_version_stderr}")
  message(FATAL_ERROR "failed to query dsdlc tool version")
endif()
string(REGEX REPLACE "^dsdlc[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)$" "\\1" LLVMDSDL_TOOL_VERSION "${dsdlc_version_stdout}")
if(NOT LLVMDSDL_TOOL_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "unexpected dsdlc --version output: '${dsdlc_version_stdout}'")
endif()

if(NOT EXISTS "${C_COMPILER}")
  message(FATAL_ERROR "C compiler not found: ${C_COMPILER}")
endif()

if(NOT EXISTS "${AR_EXECUTABLE}")
  message(FATAL_ERROR "archive tool not found: ${AR_EXECUTABLE}")
endif()

if(NOT EXISTS "${CARGO_EXECUTABLE}")
  message(FATAL_ERROR "cargo executable not found: ${CARGO_EXECUTABLE}")
endif()

if(NOT DEFINED FIXTURE_ROOT OR "${FIXTURE_ROOT}" STREQUAL "")
  set(FIXTURE_ROOT "${SOURCE_ROOT}/test/integration/fixtures/signed_narrow/vendor")
endif()
if(NOT EXISTS "${FIXTURE_ROOT}")
  message(FATAL_ERROR "signed_narrow fixture root not found: ${FIXTURE_ROOT}")
endif()

set(cargo_toml_template "${SOURCE_ROOT}/test/integration/SignedNarrowCRustParityCargo.toml.in")
set(main_rs_template "${SOURCE_ROOT}/test/integration/SignedNarrowCRustParityMain.rs")
set(c_harness_template "${SOURCE_ROOT}/test/integration/SignedNarrowCRustParityCHarness.c")
foreach(path "${cargo_toml_template}" "${main_rs_template}" "${c_harness_template}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "signed narrow C/Rust parity harness input missing: ${path}")
  endif()
endforeach()

if(NOT DEFINED ITERATIONS OR "${ITERATIONS}" STREQUAL "")
  set(ITERATIONS "256")
endif()

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(c_out "${OUT_DIR}/c")
set(rust_out "${OUT_DIR}/rust")
set(build_out "${OUT_DIR}/build")
set(harness_out "${OUT_DIR}/harness")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${rust_out}")
file(MAKE_DIRECTORY "${build_out}")
file(MAKE_DIRECTORY "${harness_out}")
file(MAKE_DIRECTORY "${harness_out}/src")

execute_process(
  COMMAND
    "${DSDLC}" --target-language c ${c_scheme_args}
      "${FIXTURE_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${c_out}"
  RESULT_VARIABLE c_result
  OUTPUT_VARIABLE c_stdout
  ERROR_VARIABLE c_stderr
)
if(NOT c_result EQUAL 0)
  message(STATUS "dsdlc c stdout:\n${c_stdout}")
  message(STATUS "dsdlc c stderr:\n${c_stderr}")
  message(FATAL_ERROR "failed to generate signed_narrow C output for C/Rust parity harness")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language rust ${other_scheme_args}
      "${FIXTURE_ROOT}"
      ${dsdlc_extra_args}
      --rust-profile "${RUST_PROFILE}"
      --rust-runtime-specialization "${RUST_RUNTIME_SPECIALIZATION}"
      --rust-memory-mode "${RUST_MEMORY_MODE}"
      --rust-inline-threshold-bytes "${RUST_INLINE_THRESHOLD_BYTES}"
      --rust-crate-name signed_narrow_generated
      --outdir "${rust_out}"
  RESULT_VARIABLE rust_result
  OUTPUT_VARIABLE rust_stdout
  ERROR_VARIABLE rust_stderr
)
if(NOT rust_result EQUAL 0)
  message(STATUS "dsdlc rust stdout:\n${rust_stdout}")
  message(STATUS "dsdlc rust stderr:\n${rust_stderr}")
  message(FATAL_ERROR "failed to generate signed_narrow Rust output for C/Rust parity harness")
endif()

set(RUST_OUT "${rust_out}")
set(RUST_DEP_FEATURES "")
if("${RUST_PROFILE}" STREQUAL "no-std-alloc")
  set(RUST_DEP_FEATURES ", default-features = false")
  if("${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
    string(APPEND RUST_DEP_FEATURES ", features = [\"runtime-fast\"]")
  endif()
endif()

configure_file("${cargo_toml_template}" "${harness_out}/Cargo.toml" @ONLY)
configure_file("${main_rs_template}" "${harness_out}/src/main.rs" @ONLY)
configure_file("${c_harness_template}" "${harness_out}/src/c_harness.c" @ONLY)

set(harness_obj "${build_out}/c_harness.o")
execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${c_out}"
      -c "${harness_out}/src/c_harness.c"
      -o "${harness_obj}"
  RESULT_VARIABLE harness_cc_result
  OUTPUT_VARIABLE harness_cc_stdout
  ERROR_VARIABLE harness_cc_stderr
)
if(NOT harness_cc_result EQUAL 0)
  message(STATUS "C harness compile stdout:\n${harness_cc_stdout}")
  message(STATUS "C harness compile stderr:\n${harness_cc_stderr}")
  message(FATAL_ERROR "failed to compile signed_narrow C/Rust parity C harness")
endif()

file(GLOB_RECURSE generated_c_sources "${c_out}/*.c")
list(LENGTH generated_c_sources generated_c_count)
if(generated_c_count EQUAL 0)
  message(FATAL_ERROR "no generated C implementation sources under ${c_out}")
endif()

set(generated_obj_dir "${build_out}/generated-obj")
file(MAKE_DIRECTORY "${generated_obj_dir}")
set(generated_objs "")
set(c_index 0)
foreach(src IN LISTS generated_c_sources)
  math(EXPR c_index "${c_index} + 1")
  set(obj "${generated_obj_dir}/generated_${c_index}.o")
  execute_process(
    COMMAND
      "${C_COMPILER}"
        -std=c11
        -Wall
        -Wextra
        -Werror
        -I "${c_out}"
        -c "${src}"
        -o "${obj}"
    RESULT_VARIABLE c_cc_result
    OUTPUT_VARIABLE c_cc_stdout
    ERROR_VARIABLE c_cc_stderr
  )
  if(NOT c_cc_result EQUAL 0)
    message(STATUS "failed source: ${src}")
    message(STATUS "generated C compile stdout:\n${c_cc_stdout}")
    message(STATUS "generated C compile stderr:\n${c_cc_stderr}")
    message(FATAL_ERROR "failed to compile generated C implementation for signed_narrow C/Rust parity")
  endif()
  list(APPEND generated_objs "${obj}")
endforeach()

set(static_lib "${build_out}/libllvmdsdl_signed_narrow_c_rust_parity.a")
execute_process(
  COMMAND "${AR_EXECUTABLE}" rcs "${static_lib}" "${harness_obj}" ${generated_objs}
  RESULT_VARIABLE ar_result
  OUTPUT_VARIABLE ar_stdout
  ERROR_VARIABLE ar_stderr
)
if(NOT ar_result EQUAL 0)
  message(STATUS "archive stdout:\n${ar_stdout}")
  message(STATUS "archive stderr:\n${ar_stderr}")
  message(FATAL_ERROR "failed to archive signed_narrow C/Rust parity support library")
endif()
if(NOT EXISTS "${static_lib}")
  message(FATAL_ERROR "signed_narrow C/Rust parity archive missing after creation: ${static_lib}")
endif()

set(rustflags "-L native=${build_out} -l static=llvmdsdl_signed_narrow_c_rust_parity")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "RUSTFLAGS=${rustflags}"
      "CARGO_TARGET_DIR=${build_out}/cargo-target"
      "${CARGO_EXECUTABLE}" run --quiet --manifest-path "${harness_out}/Cargo.toml" -- "${ITERATIONS}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
  message(STATUS "cargo run stdout:\n${run_stdout}")
  message(STATUS "cargo run stderr:\n${run_stderr}")
  message(FATAL_ERROR "signed_narrow C/Rust parity harness reported mismatches")
endif()

set(min_iterations 256)
set(min_random_cases 2)
set(min_directed_cases 1)
string(REGEX MATCH
  "PASS signed-narrow-c-rust parity random_iterations=([0-9]+) random_cases=([0-9]+) directed_cases=([0-9]+)"
  summary_line
  "${run_stdout}")
if(NOT summary_line)
  message(FATAL_ERROR
    "failed to parse signed_narrow C/Rust parity summary line from harness output")
endif()
set(observed_iterations "${CMAKE_MATCH_1}")
set(observed_random_cases "${CMAKE_MATCH_2}")
set(observed_directed_cases "${CMAKE_MATCH_3}")
if(observed_iterations LESS min_iterations)
  message(FATAL_ERROR
    "signed_narrow C/Rust parity iteration regression: observed=${observed_iterations}, required>=${min_iterations}")
endif()
if(observed_random_cases LESS min_random_cases)
  message(FATAL_ERROR
    "signed_narrow C/Rust parity random-case regression: observed=${observed_random_cases}, required>=${min_random_cases}")
endif()
if(observed_directed_cases LESS min_directed_cases)
  message(FATAL_ERROR
    "signed_narrow C/Rust parity directed-case regression: observed=${observed_directed_cases}, required>=${min_directed_cases}")
endif()

string(REGEX MATCH
  "PASS signed-narrow-c-rust inventory random_cases=([0-9]+) directed_cases=([0-9]+)"
  inventory_line
  "${run_stdout}")
if(NOT inventory_line)
  message(FATAL_ERROR "missing signed_narrow C/Rust parity inventory marker")
endif()
set(inventory_random_cases "${CMAKE_MATCH_1}")
set(inventory_directed_cases "${CMAKE_MATCH_2}")
if(NOT inventory_random_cases EQUAL observed_random_cases OR
   NOT inventory_directed_cases EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "signed_narrow C/Rust inventory mismatch: inventory random=${inventory_random_cases}, "
    "inventory directed=${inventory_directed_cases}, summary random=${observed_random_cases}, "
    "summary directed=${observed_directed_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_.]+ random \\([0-9]+ iterations\\)"
  random_pass_lines
  "${run_stdout}")
list(LENGTH random_pass_lines observed_random_pass_lines)
if(NOT observed_random_pass_lines EQUAL observed_random_cases)
  message(FATAL_ERROR
    "signed_narrow C/Rust random execution count mismatch: pass-lines=${observed_random_pass_lines}, "
    "summary random=${observed_random_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_]+ directed"
  directed_pass_lines
  "${run_stdout}")
list(LENGTH directed_pass_lines observed_directed_pass_lines)
if(NOT observed_directed_pass_lines EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "signed_narrow C/Rust directed execution count mismatch: pass-lines=${observed_directed_pass_lines}, "
    "summary directed=${observed_directed_cases}")
endif()

set(required_markers
  "PASS vendor.Int3Sat.1.0 random ("
  "PASS vendor.Int3Trunc.1.0 random ("
  "PASS signed_narrow_directed_c_rust directed"
  "INFO signed-narrow-c-rust directed marker int3sat_serialize_plus7_saturated"
  "INFO signed-narrow-c-rust directed marker int3sat_serialize_minus9_saturated"
  "INFO signed-narrow-c-rust directed marker int3trunc_serialize_plus5_truncated"
  "INFO signed-narrow-c-rust directed marker int3trunc_serialize_minus5_truncated"
  "INFO signed-narrow-c-rust directed marker int3sat_sign_extend_0x07"
  "INFO signed-narrow-c-rust directed marker int3sat_sign_extend_0x04"
  "INFO signed-narrow-c-rust directed marker int3trunc_sign_extend_0x05"
  "INFO signed-narrow-c-rust directed marker int3trunc_sign_extend_0x03"
)
foreach(marker IN LISTS required_markers)
  string(FIND "${run_stdout}" "${marker}" marker_pos)
  if(marker_pos EQUAL -1)
    message(FATAL_ERROR
      "required signed_narrow C/Rust parity marker missing: ${marker}")
  endif()
endforeach()

set(summary_file "${OUT_DIR}/signed-narrow-c-rust-parity-summary.txt")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef summary_nonce)
set(summary_tmp "${summary_file}.tmp-${summary_nonce}")
file(WRITE "${summary_tmp}" "${run_stdout}\n")
file(RENAME "${summary_tmp}" "${summary_file}")
message(STATUS "Signed narrow C/Rust parity summary:\n${run_stdout}")
