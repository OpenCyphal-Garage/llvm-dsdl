cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR C_COMPILER CXX_COMPILER SOURCE_ROOT)
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

if(NOT EXISTS "${CXX_COMPILER}")
  message(FATAL_ERROR "C++ compiler not found: ${CXX_COMPILER}")
endif()

if(NOT DEFINED CPP_PROFILE OR "${CPP_PROFILE}" STREQUAL "")
  set(CPP_PROFILE "std")
endif()
if(NOT (CPP_PROFILE STREQUAL "std" OR CPP_PROFILE STREQUAL "pmr" OR CPP_PROFILE STREQUAL "autosar"))
  message(FATAL_ERROR "CPP_PROFILE must be one of: std, pmr, autosar")
endif()

if(CPP_PROFILE STREQUAL "autosar")
  set(cxx_std_flag -std=c++14)
  set(cxx_warning_flags
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      -Werror)
else()
  set(cxx_std_flag -std=c++23)
  set(cxx_warning_flags
      -Wall
      -Wextra
      -Werror)
endif()

if(NOT DEFINED FIXTURE_ROOT OR "${FIXTURE_ROOT}" STREQUAL "")
  set(FIXTURE_ROOT "${SOURCE_ROOT}/test/integration/fixtures/signed_narrow/vendor")
endif()
if(NOT EXISTS "${FIXTURE_ROOT}")
  message(FATAL_ERROR "signed_narrow fixture root not found: ${FIXTURE_ROOT}")
endif()

# The harness names generated types, so it is configured after the output tree is made; check the
# template here, and the configured copy where it is produced.
if(NOT EXISTS "${SOURCE_ROOT}/test/integration/SignedNarrowCppCParityMain.cpp")
  message(FATAL_ERROR "signed narrow parity harness source missing")
endif()

if(NOT DEFINED ITERATIONS OR "${ITERATIONS}" STREQUAL "")
  set(ITERATIONS "256")
endif()

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

if(NOT DEFINED C_TYPE_NAME_SCHEME)
  set(C_TYPE_NAME_SCHEME "unversioned")
endif()
if(NOT DEFINED TYPE_NAME_SCHEME)
  set(TYPE_NAME_SCHEME "unversioned")
endif()
include("${SOURCE_ROOT}/cmake/HarnessTypeNameTokens.cmake")
llvmdsdl_harness_type_name_tokens("${C_TYPE_NAME_SCHEME}" "${TYPE_NAME_SCHEME}")
set(parity_main "${OUT_DIR}/SignedNarrowCppCParityMain.cpp")
configure_file("${SOURCE_ROOT}/test/integration/SignedNarrowCppCParityMain.cpp" "${parity_main}" @ONLY)

set(c_out "${OUT_DIR}/c")
set(cpp_out "${OUT_DIR}/cpp")
set(build_out "${OUT_DIR}/build")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${cpp_out}")
file(MAKE_DIRECTORY "${build_out}")

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
  message(FATAL_ERROR "failed to generate signed_narrow C output for parity harness")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language cpp
      "${FIXTURE_ROOT}"
      ${dsdlc_extra_args}
      --cpp-profile "${CPP_PROFILE}"
      --outdir "${cpp_out}"
  RESULT_VARIABLE cpp_result
  OUTPUT_VARIABLE cpp_stdout
  ERROR_VARIABLE cpp_stderr
)
if(NOT cpp_result EQUAL 0)
  message(STATUS "dsdlc cpp stdout:\n${cpp_stdout}")
  message(STATUS "dsdlc cpp stderr:\n${cpp_stderr}")
  message(FATAL_ERROR "failed to generate signed_narrow C++ ${CPP_PROFILE} output for parity harness")
endif()

set(main_obj "${build_out}/signed_narrow_cpp_c_parity_main.o")
execute_process(
  COMMAND
    "${CXX_COMPILER}"
      ${cxx_std_flag}
      ${cxx_warning_flags}
      -I "${c_out}"
      -I "${cpp_out}"
      -c "${parity_main}"
      -o "${main_obj}"
  RESULT_VARIABLE main_cc_result
  OUTPUT_VARIABLE main_cc_stdout
  ERROR_VARIABLE main_cc_stderr
)
if(NOT main_cc_result EQUAL 0)
  message(STATUS "C++ compile stdout:\n${main_cc_stdout}")
  message(STATUS "C++ compile stderr:\n${main_cc_stderr}")
  message(FATAL_ERROR "failed to compile signed_narrow C/C++ parity harness main")
endif()

file(GLOB_RECURSE generated_c_sources "${c_out}/*.c")
list(LENGTH generated_c_sources generated_c_count)
if(generated_c_count EQUAL 0)
  message(FATAL_ERROR "no generated C implementation sources under ${c_out}")
endif()

set(c_obj_dir "${build_out}/cobj")
file(MAKE_DIRECTORY "${c_obj_dir}")
set(c_objs "")
set(c_index 0)
foreach(src IN LISTS generated_c_sources)
  math(EXPR c_index "${c_index} + 1")
  set(obj "${c_obj_dir}/generated_${c_index}.o")
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
    message(STATUS "C compile stdout:\n${c_cc_stdout}")
    message(STATUS "C compile stderr:\n${c_cc_stderr}")
    message(FATAL_ERROR "failed to compile generated signed_narrow C implementation source")
  endif()
  list(APPEND c_objs "${obj}")
endforeach()

set(exe "${build_out}/signed_narrow_cpp_c_parity_runner")
execute_process(
  COMMAND
    "${CXX_COMPILER}"
      "${main_obj}"
      ${c_objs}
      -o "${exe}"
  RESULT_VARIABLE link_result
  OUTPUT_VARIABLE link_stdout
  ERROR_VARIABLE link_stderr
)
if(NOT link_result EQUAL 0)
  message(STATUS "link stdout:\n${link_stdout}")
  message(STATUS "link stderr:\n${link_stderr}")
  message(FATAL_ERROR "failed to link signed_narrow C/C++ parity harness")
endif()

execute_process(
  COMMAND "${exe}" "${ITERATIONS}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
  message(STATUS "runner stdout:\n${run_stdout}")
  message(STATUS "runner stderr:\n${run_stderr}")
  message(FATAL_ERROR "signed_narrow C/C++ parity harness reported mismatches")
endif()

set(min_iterations 256)
set(min_random_cases 2)
set(min_directed_cases 1)
string(REGEX MATCH
  "PASS signed-narrow-cpp-c parity random_iterations=([0-9]+) random_cases=([0-9]+) directed_cases=([0-9]+)"
  summary_line
  "${run_stdout}")
if(NOT summary_line)
  message(FATAL_ERROR
    "failed to parse signed_narrow C/C++ parity summary line from harness output")
endif()
set(observed_iterations "${CMAKE_MATCH_1}")
set(observed_random_cases "${CMAKE_MATCH_2}")
set(observed_directed_cases "${CMAKE_MATCH_3}")
if(observed_iterations LESS min_iterations)
  message(FATAL_ERROR
    "signed_narrow C/C++ parity iteration regression: observed=${observed_iterations}, required>=${min_iterations}")
endif()
if(observed_random_cases LESS min_random_cases)
  message(FATAL_ERROR
    "signed_narrow C/C++ parity random-case regression: observed=${observed_random_cases}, required>=${min_random_cases}")
endif()
if(observed_directed_cases LESS min_directed_cases)
  message(FATAL_ERROR
    "signed_narrow C/C++ parity directed-case regression: observed=${observed_directed_cases}, required>=${min_directed_cases}")
endif()

string(REGEX MATCH
  "PASS signed-narrow-cpp-c inventory random_cases=([0-9]+) directed_cases=([0-9]+)"
  inventory_line
  "${run_stdout}")
if(NOT inventory_line)
  message(FATAL_ERROR "missing signed_narrow C/C++ parity inventory marker")
endif()
set(inventory_random_cases "${CMAKE_MATCH_1}")
set(inventory_directed_cases "${CMAKE_MATCH_2}")
if(NOT inventory_random_cases EQUAL observed_random_cases OR
   NOT inventory_directed_cases EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "signed_narrow C/C++ inventory mismatch: inventory random=${inventory_random_cases}, "
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
    "signed_narrow C/C++ random execution count mismatch: pass-lines=${observed_random_pass_lines}, "
    "summary random=${observed_random_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_]+ directed"
  directed_pass_lines
  "${run_stdout}")
list(LENGTH directed_pass_lines observed_directed_pass_lines)
if(NOT observed_directed_pass_lines EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "signed_narrow C/C++ directed execution count mismatch: pass-lines=${observed_directed_pass_lines}, "
    "summary directed=${observed_directed_cases}")
endif()

set(required_markers
  "PASS vendor.Int3Sat.1.0 random ("
  "PASS vendor.Int3Trunc.1.0 random ("
  "PASS signed_narrow_directed_cpp_c directed"
  "INFO signed-narrow-cpp-c directed marker int3sat_serialize_plus7_saturated"
  "INFO signed-narrow-cpp-c directed marker int3sat_serialize_minus9_saturated"
  "INFO signed-narrow-cpp-c directed marker int3trunc_serialize_plus5_truncated"
  "INFO signed-narrow-cpp-c directed marker int3trunc_serialize_minus5_truncated"
  "INFO signed-narrow-cpp-c directed marker int3sat_sign_extend_0x07"
  "INFO signed-narrow-cpp-c directed marker int3sat_sign_extend_0x04"
  "INFO signed-narrow-cpp-c directed marker int3trunc_sign_extend_0x05"
  "INFO signed-narrow-cpp-c directed marker int3trunc_sign_extend_0x03"
)
foreach(marker IN LISTS required_markers)
  string(FIND "${run_stdout}" "${marker}" marker_pos)
  if(marker_pos EQUAL -1)
    message(FATAL_ERROR
      "required signed_narrow C/C++ parity marker missing: ${marker}")
  endif()
endforeach()

set(summary_file "${OUT_DIR}/signed-narrow-cpp-c-parity-${CPP_PROFILE}-summary.txt")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef summary_nonce)
set(summary_tmp "${summary_file}.tmp-${summary_nonce}")
file(WRITE "${summary_tmp}" "${run_stdout}\n")
file(RENAME "${summary_tmp}" "${summary_file}")
message(STATUS "Signed narrow C/C++ (${CPP_PROFILE}) parity summary:\n${run_stdout}")
