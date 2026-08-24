cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR C_COMPILER AR_EXECUTABLE GO_EXECUTABLE SOURCE_ROOT)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()

if(NOT EXISTS "${C_COMPILER}")
  message(FATAL_ERROR "C compiler not found: ${C_COMPILER}")
endif()

if(NOT EXISTS "${AR_EXECUTABLE}")
  message(FATAL_ERROR "archive tool not found: ${AR_EXECUTABLE}")
endif()

if(NOT EXISTS "${GO_EXECUTABLE}")
  message(FATAL_ERROR "go executable not found: ${GO_EXECUTABLE}")
endif()

if(NOT DEFINED FIXTURE_ROOT OR "${FIXTURE_ROOT}" STREQUAL "")
  set(FIXTURE_ROOT "${SOURCE_ROOT}/test/integration/fixtures/signed_narrow/vendor")
endif()
if(NOT EXISTS "${FIXTURE_ROOT}")
  message(FATAL_ERROR "signed_narrow fixture root not found: ${FIXTURE_ROOT}")
endif()

set(go_mod_template "${SOURCE_ROOT}/test/integration/SignedNarrowCGoParityGo.mod.in")
set(main_go_template "${SOURCE_ROOT}/test/integration/SignedNarrowCGoParityMain.go")
set(c_harness_template "${SOURCE_ROOT}/test/integration/SignedNarrowCGoParityCHarness.c")
foreach(path "${go_mod_template}" "${main_go_template}" "${c_harness_template}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "signed narrow C/Go parity harness input missing: ${path}")
  endif()
endforeach()

if(NOT DEFINED ITERATIONS OR "${ITERATIONS}" STREQUAL "")
  set(ITERATIONS "256")
endif()

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")
foreach(stale_dir c go build harness .gocache .gomodcache)
  if(EXISTS "${OUT_DIR}/${stale_dir}")
    file(REMOVE_RECURSE "${OUT_DIR}/${stale_dir}")
  endif()
endforeach()
string(TIMESTAMP parity_run_timestamp "%Y%m%d%H%M%S")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef parity_run_nonce)
set(run_out "${OUT_DIR}/run-${parity_run_timestamp}-${parity_run_nonce}")
file(MAKE_DIRECTORY "${run_out}")

set(c_out "${run_out}/c")
set(go_out "${run_out}/go")
set(build_out "${run_out}/build")
set(harness_out "${run_out}/harness")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${go_out}")
file(MAKE_DIRECTORY "${build_out}")
file(MAKE_DIRECTORY "${harness_out}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language c
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
  message(FATAL_ERROR "failed to generate signed_narrow C output for C/Go parity harness")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language go
      "${FIXTURE_ROOT}"
      ${dsdlc_extra_args}
      --go-module "signed_narrow_generated"
      --outdir "${go_out}"
  RESULT_VARIABLE go_result
  OUTPUT_VARIABLE go_stdout
  ERROR_VARIABLE go_stderr
)
if(NOT go_result EQUAL 0)
  message(STATUS "dsdlc go stdout:\n${go_stdout}")
  message(STATUS "dsdlc go stderr:\n${go_stderr}")
  message(FATAL_ERROR "failed to generate signed_narrow Go output for C/Go parity harness")
endif()

set(GO_OUT "${go_out}")
if(NOT DEFINED C_TYPE_NAME_SCHEME)
  set(C_TYPE_NAME_SCHEME "unversioned")
endif()
if(NOT DEFINED TYPE_NAME_SCHEME)
  set(TYPE_NAME_SCHEME "versioned")
endif()
include("${SOURCE_ROOT}/cmake/HarnessTypeNameTokens.cmake")
llvmdsdl_harness_type_name_tokens("${C_TYPE_NAME_SCHEME}" "${TYPE_NAME_SCHEME}")

configure_file("${go_mod_template}" "${harness_out}/go.mod" @ONLY)
configure_file("${main_go_template}" "${harness_out}/main.go" @ONLY)
set(c_harness_src "${build_out}/c_harness.c")
configure_file("${c_harness_template}" "${c_harness_src}" @ONLY)

set(harness_obj "${build_out}/c_harness.o")
execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${c_out}"
      -c "${c_harness_src}"
      -o "${harness_obj}"
  RESULT_VARIABLE harness_cc_result
  OUTPUT_VARIABLE harness_cc_stdout
  ERROR_VARIABLE harness_cc_stderr
)
if(NOT harness_cc_result EQUAL 0)
  message(STATUS "C harness compile stdout:\n${harness_cc_stdout}")
  message(STATUS "C harness compile stderr:\n${harness_cc_stderr}")
  message(FATAL_ERROR "failed to compile signed_narrow C/Go parity C harness")
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
    message(FATAL_ERROR "failed to compile generated C implementation for signed_narrow C/Go parity")
  endif()
  list(APPEND generated_objs "${obj}")
endforeach()

set(static_lib "${build_out}/libllvmdsdl_signed_narrow_c_go_parity.a")
execute_process(
  COMMAND "${AR_EXECUTABLE}" rcs "${static_lib}" "${harness_obj}" ${generated_objs}
  RESULT_VARIABLE ar_result
  OUTPUT_VARIABLE ar_stdout
  ERROR_VARIABLE ar_stderr
)
if(NOT ar_result EQUAL 0)
  message(STATUS "archive stdout:\n${ar_stdout}")
  message(STATUS "archive stderr:\n${ar_stderr}")
  message(FATAL_ERROR "failed to archive signed_narrow C/Go parity support library")
endif()
if(NOT EXISTS "${static_lib}")
  message(FATAL_ERROR "signed_narrow C/Go parity archive missing after creation: ${static_lib}")
endif()

set(go_cache "${run_out}/.gocache")
set(go_mod_cache "${run_out}/.gomodcache")
set(ext_ldflags "${static_lib}")
set(go_ldflags "-extldflags '${ext_ldflags}'")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "CC=${C_COMPILER}"
      "CGO_ENABLED=1"
      "GOCACHE=${go_cache}"
      "GOMODCACHE=${go_mod_cache}"
      "${GO_EXECUTABLE}" run -ldflags "${go_ldflags}" . "${ITERATIONS}"
  WORKING_DIRECTORY "${harness_out}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
  message(STATUS "go run stdout:\n${run_stdout}")
  message(STATUS "go run stderr:\n${run_stderr}")
  message(FATAL_ERROR "signed_narrow C/Go parity harness reported mismatches")
endif()

set(min_iterations 256)
set(min_random_cases 2)
set(min_directed_cases 12)
string(REGEX MATCH
  "PASS signed-narrow-c-go-parity random_iterations=([0-9]+) random_cases=([0-9]+) directed_cases=([0-9]+)"
  summary_line
  "${run_stdout}")
if(NOT summary_line)
  message(FATAL_ERROR
    "failed to parse signed_narrow C/Go parity summary line from harness output")
endif()
set(observed_iterations "${CMAKE_MATCH_1}")
set(observed_random_cases "${CMAKE_MATCH_2}")
set(observed_directed_cases "${CMAKE_MATCH_3}")
if(observed_iterations LESS min_iterations)
  message(FATAL_ERROR
    "signed_narrow C/Go parity iteration regression: observed=${observed_iterations}, required>=${min_iterations}")
endif()
if(observed_random_cases LESS min_random_cases)
  message(FATAL_ERROR
    "signed_narrow C/Go parity random-case regression: observed=${observed_random_cases}, required>=${min_random_cases}")
endif()
if(observed_directed_cases LESS min_directed_cases)
  message(FATAL_ERROR
    "signed_narrow C/Go parity directed-case regression: observed=${observed_directed_cases}, required>=${min_directed_cases}")
endif()

string(REGEX MATCH
  "PASS signed-narrow-c-go-parity inventory random_cases=([0-9]+) directed_cases=([0-9]+)"
  inventory_line
  "${run_stdout}")
if(NOT inventory_line)
  message(FATAL_ERROR
    "missing signed_narrow C/Go parity inventory marker")
endif()
set(inventory_random_cases "${CMAKE_MATCH_1}")
set(inventory_directed_cases "${CMAKE_MATCH_2}")
if(NOT inventory_random_cases EQUAL observed_random_cases OR
   NOT inventory_directed_cases EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "signed_narrow inventory mismatch: inventory random=${inventory_random_cases}, "
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
    "signed_narrow random execution count mismatch: pass-lines=${observed_random_pass_lines}, "
    "summary random=${observed_random_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_]+ directed"
  directed_pass_lines
  "${run_stdout}")
list(LENGTH directed_pass_lines observed_directed_pass_lines)
if(NOT observed_directed_pass_lines EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "signed_narrow directed execution count mismatch: pass-lines=${observed_directed_pass_lines}, "
    "summary directed=${observed_directed_cases}")
endif()

set(required_directed_category_mins
  "saturation_sign_extension:8"
  "truncation:2"
  "serialize_buffer:2"
)
foreach(spec IN LISTS required_directed_category_mins)
  string(REPLACE ":" ";" parts "${spec}")
  list(GET parts 0 key)
  list(GET parts 1 min_value)
  string(REGEX MATCH "${key}=([0-9]+)" key_match "${run_stdout}")
  if(NOT key_match)
    message(FATAL_ERROR
      "missing signed_narrow directed parity category count: ${key}")
  endif()
  set(observed_value "${CMAKE_MATCH_1}")
  if(observed_value LESS min_value)
    message(FATAL_ERROR
      "signed_narrow directed parity category regression for ${key}: observed=${observed_value}, required>=${min_value}")
  endif()
endforeach()

set(required_markers
  "PASS vendor.Int3Sat.1.0 random ("
  "PASS vendor.Int3Trunc.1.0 random ("
  "PASS int3sat_serialize_plus7_saturated directed"
  "PASS int3sat_serialize_minus9_saturated directed"
  "PASS int3trunc_serialize_plus5_truncated directed"
  "PASS int3trunc_serialize_minus5_truncated directed"
  "PASS int3sat_sign_extend_0x07 directed"
  "PASS int3sat_sign_extend_0x04 directed"
  "PASS int3trunc_sign_extend_0x05 directed"
  "PASS int3trunc_sign_extend_0x03 directed"
  "PASS int3sat_truncated_input directed"
  "PASS int3trunc_truncated_input directed"
  "PASS int3sat_serialize_small_buffer directed"
  "PASS int3trunc_serialize_small_buffer directed"
  "PASS signed-narrow-directed"
)
foreach(marker IN LISTS required_markers)
  string(FIND "${run_stdout}" "${marker}" marker_pos)
  if(marker_pos EQUAL -1)
    message(FATAL_ERROR
      "required signed_narrow C/Go parity marker missing: ${marker}")
  endif()
endforeach()

set(summary_file "${OUT_DIR}/signed-narrow-c-go-parity-summary.txt")
set(summary_tmp "${summary_file}.tmp-${parity_run_nonce}")
file(WRITE "${summary_tmp}" "${run_stdout}\n")
file(RENAME "${summary_tmp}" "${summary_file}")
file(REMOVE_RECURSE "${run_out}")
if(EXISTS "${run_out}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -rf "${run_out}")
endif()
if(EXISTS "${run_out}")
  message(WARNING "unable to remove signed_narrow parity scratch directory: ${run_out}")
endif()
message(STATUS "Signed narrow C/Go parity summary:\n${run_stdout}")
