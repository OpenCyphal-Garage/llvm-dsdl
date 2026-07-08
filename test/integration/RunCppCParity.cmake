cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR C_COMPILER CXX_COMPILER SOURCE_ROOT)
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

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

# Optional sanitizer instrumentation of the generated C/C++ decoders. SANITIZE is
# a comma-separated -fsanitize list (e.g. "address,undefined"); empty = off. When
# set, both the generated-code objects and the harness main are built and linked
# with the sanitizer so ASan/UBSan observe the decode path over the randomized +
# directed malformed inputs the harness already drives. -fno-sanitize-recover=all
# makes UBSan findings fatal (nonzero exit) instead of merely logged.
set(san_compile_flags "")
set(san_link_flags "")
if(DEFINED SANITIZE AND NOT "${SANITIZE}" STREQUAL "")
  set(san_compile_flags
      "-fsanitize=${SANITIZE}"
      -fno-sanitize-recover=all
      -fno-omit-frame-pointer
      -g)
  set(san_link_flags "-fsanitize=${SANITIZE}")
  message(STATUS "C/C++ parity harness sanitized with: ${SANITIZE}")
endif()

set(parity_main "${SOURCE_ROOT}/test/integration/CppCParityMain.cpp")
if(NOT EXISTS "${parity_main}")
  message(FATAL_ERROR "parity harness source missing: ${parity_main}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(c_out "${OUT_DIR}/c")
set(cpp_out "${OUT_DIR}/cpp")
set(build_out "${OUT_DIR}/build")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${cpp_out}")
file(MAKE_DIRECTORY "${build_out}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language c
      "${UAVCAN_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${c_out}"
  RESULT_VARIABLE c_result
  OUTPUT_VARIABLE c_stdout
  ERROR_VARIABLE c_stderr
)
if(NOT c_result EQUAL 0)
  message(STATUS "dsdlc c stdout:\n${c_stdout}")
  message(STATUS "dsdlc c stderr:\n${c_stderr}")
  message(FATAL_ERROR "failed to generate C output for parity harness")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language cpp
      "${UAVCAN_ROOT}"
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
  message(FATAL_ERROR "failed to generate C++ ${CPP_PROFILE} output for parity harness")
endif()

set(main_obj "${build_out}/cpp_c_parity_main.o")
execute_process(
  COMMAND
    "${CXX_COMPILER}"
      ${cxx_std_flag}
      ${cxx_warning_flags}
      ${san_compile_flags}
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
  message(FATAL_ERROR "failed to compile C/C++ parity harness main")
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
        ${san_compile_flags}
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
    message(FATAL_ERROR "failed to compile generated C implementation source")
  endif()
  list(APPEND c_objs "${obj}")
endforeach()

set(exe "${build_out}/cpp_c_parity_runner")
execute_process(
  COMMAND
    "${CXX_COMPILER}"
      "${main_obj}"
      ${c_objs}
      ${san_link_flags}
      -o "${exe}"
  RESULT_VARIABLE link_result
  OUTPUT_VARIABLE link_stdout
  ERROR_VARIABLE link_stderr
)
if(NOT link_result EQUAL 0)
  message(STATUS "link stdout:\n${link_stdout}")
  message(STATUS "link stderr:\n${link_stderr}")
  message(FATAL_ERROR "failed to link C/C++ parity harness")
endif()

execute_process(
  COMMAND "${exe}" 128
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
  message(STATUS "runner stdout:\n${run_stdout}")
  message(STATUS "runner stderr:\n${run_stderr}")
  message(FATAL_ERROR "C/C++ parity harness reported mismatches")
endif()

set(min_random 128)
set(min_cases 7)
set(min_directed 1)
string(REGEX MATCH
  "PASS cpp-c parity random_iterations=([0-9]+) random_cases=([0-9]+) directed_cases=([0-9]+)"
  parity_summary_line
  "${run_stdout}")
if(NOT parity_summary_line)
  message(FATAL_ERROR
    "failed to parse C/C++ parity summary line from harness output")
endif()
set(observed_random "${CMAKE_MATCH_1}")
set(observed_cases "${CMAKE_MATCH_2}")
set(observed_directed "${CMAKE_MATCH_3}")
if(observed_random LESS min_random)
  message(FATAL_ERROR
    "C/C++ parity random-iteration regression: observed=${observed_random}, required>=${min_random}")
endif()
if(observed_cases LESS min_cases)
  message(FATAL_ERROR
    "C/C++ parity case count regression: observed=${observed_cases}, required>=${min_cases}")
endif()
if(observed_directed LESS min_directed)
  message(FATAL_ERROR
    "C/C++ parity directed count regression: observed=${observed_directed}, required>=${min_directed}")
endif()

string(REGEX MATCH
  "PASS cpp-c inventory random_cases=([0-9]+) directed_cases=([0-9]+)"
  inventory_summary_match
  "${run_stdout}")
if(NOT inventory_summary_match)
  message(FATAL_ERROR "missing C/C++ parity inventory summary marker")
endif()
set(inventory_cases "${CMAKE_MATCH_1}")
set(inventory_directed "${CMAKE_MATCH_2}")
if(NOT inventory_cases EQUAL observed_cases OR
   NOT inventory_directed EQUAL observed_directed)
  message(FATAL_ERROR
    "C/C++ parity inventory mismatch: inventory cases=${inventory_cases}, "
    "inventory directed=${inventory_directed}, summary cases=${observed_cases}, "
    "summary directed=${observed_directed}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_.]+ random \\([0-9]+ iterations\\)"
  random_pass_lines
  "${run_stdout}")
list(LENGTH random_pass_lines observed_random_pass_lines)
if(NOT observed_random_pass_lines EQUAL observed_cases)
  message(FATAL_ERROR
    "C/C++ random execution count mismatch: pass-lines=${observed_random_pass_lines}, "
    "summary cases=${observed_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_]+ directed"
  directed_pass_lines
  "${run_stdout}")
list(LENGTH directed_pass_lines observed_directed_pass_lines)
if(NOT observed_directed_pass_lines EQUAL observed_directed)
  message(FATAL_ERROR
    "C/C++ directed execution count mismatch: pass-lines=${observed_directed_pass_lines}, "
    "summary directed=${observed_directed}")
endif()

set(required_directed_markers
  "INFO cpp-c directed marker heartbeat_empty_deserialize"
  "INFO cpp-c directed marker frame_bad_union_tag_deserialize"
  "INFO cpp-c directed marker execute_request_truncated_payload_roundtrip"
  "INFO cpp-c directed marker execute_response_truncated_payload_roundtrip"
  "INFO cpp-c directed marker execute_response_bad_array_length_deserialize"
  "INFO cpp-c directed marker list_bad_delimiter_header_deserialize"
  "INFO cpp-c directed marker list_second_delimiter_header_deserialize"
  "INFO cpp-c directed marker list_nested_bad_union_tag_deserialize"
  "INFO cpp-c directed marker list_second_section_nested_bad_union_tag_deserialize"
  "INFO cpp-c directed marker list_third_delimiter_header_deserialize"
  "INFO cpp-c directed marker list_nested_bad_array_length_serialize"
  "INFO cpp-c directed marker frame_bad_union_tag_serialize"
  "INFO cpp-c directed marker execute_request_too_small_serialize"
  "INFO cpp-c directed marker execute_request_bad_array_length_serialize"
  "INFO cpp-c directed marker execute_response_bad_array_length_serialize"
  "INFO cpp-c directed marker heartbeat_too_small_serialize"
  "INFO cpp-c directed marker health_saturating_serialize"
  "INFO cpp-c directed marker synchronized_timestamp_truncating_serialize"
  "INFO cpp-c directed marker integer8_signed_roundtrip"
)
foreach(marker IN LISTS required_directed_markers)
  string(FIND "${run_stdout}" "${marker}" marker_pos)
  if(marker_pos EQUAL -1)
    message(FATAL_ERROR
      "required C/C++ directed parity marker missing: ${marker}")
  endif()
endforeach()

set(summary_file "${OUT_DIR}/cpp-c-parity-${CPP_PROFILE}-summary.txt")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef summary_nonce)
set(summary_tmp "${summary_file}.tmp-${summary_nonce}")
file(WRITE "${summary_tmp}" "${run_stdout}\n")
file(RENAME "${summary_tmp}" "${summary_file}")
message(STATUS "C/C++ (${CPP_PROFILE}) parity summary:\n${run_stdout}")
