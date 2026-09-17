# Differential parity vs Nunavut — CORROBORATION, not an oracle.
#
# The Cyphal Specification is the truth; spec/dafny/CyphalSerdes.dfy is the
# machine-checked oracle (see its Authority hierarchy). Nunavut is a pinned peer
# implementation: byte agreement here corroborates both implementations, and a
# mismatch is an investigation adjudicated by the specification — it is as likely
# to be a Nunavut defect as ours. The 10 cases cover every wire-shape class;
# spec-derived verification covers the rest.
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR C_COMPILER PYTHON_EXECUTABLE SOURCE_ROOT NUNAVUT_REPO PYDSDL_REPO)
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

if(NOT EXISTS "${PYTHON_EXECUTABLE}")
  message(FATAL_ERROR "Python executable not found: ${PYTHON_EXECUTABLE}")
endif()

if(NOT EXISTS "${NUNAVUT_REPO}/src/nunavut")
  message(FATAL_ERROR "nunavut source tree not found: ${NUNAVUT_REPO}")
endif()

if(NOT EXISTS "${PYDSDL_REPO}/pydsdl")
  message(FATAL_ERROR "pydsdl source tree not found: ${PYDSDL_REPO}")
endif()

set(main_template "${SOURCE_ROOT}/test/integration/DifferentialParityMain.c.in")
if(NOT EXISTS "${main_template}")
  message(FATAL_ERROR "Differential harness input missing: ${main_template}")
endif()

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

# Generates both C trees and compiles the two case tables against them. Shared
# with the instruction-count comparison, so both lanes cover the same ten types
# built the same way.
include("${CMAKE_CURRENT_LIST_DIR}/DifferentialCorpus.cmake")
llvmdsdl_differential_corpus(
  OUT_DIR "${OUT_DIR}"
  DSDLC_EXTRA_ARGS ${dsdlc_extra_args}
  OBJECTS_VAR corpus_objects
  BUILD_DIR_VAR build_out
)

set(main_c "${build_out}/differential_parity_main.c")
configure_file("${main_template}" "${main_c}" @ONLY)

set(main_obj "${build_out}/differential_parity_main.o")
execute_process(
  COMMAND
    "${C_COMPILER}"
      ${LLVMDSDL_DIFFERENTIAL_BASE_C_FLAGS}
      -I "${SOURCE_ROOT}/test/integration"
      -c "${main_c}"
      -o "${main_obj}"
  RESULT_VARIABLE cc_result
  OUTPUT_VARIABLE cc_stdout
  ERROR_VARIABLE cc_stderr
)
if(NOT cc_result EQUAL 0)
  message(STATUS "compiler stdout:\n${cc_stdout}")
  message(STATUS "compiler stderr:\n${cc_stderr}")
  message(FATAL_ERROR "failed to compile differential parity main unit")
endif()

set(harness_exe "${build_out}/differential_parity_runner")
execute_process(
  COMMAND
    "${C_COMPILER}"
      "${main_obj}"
      ${corpus_objects}
      -o "${harness_exe}"
  RESULT_VARIABLE link_result
  OUTPUT_VARIABLE link_stdout
  ERROR_VARIABLE link_stderr
)
if(NOT link_result EQUAL 0)
  message(STATUS "link stdout:\n${link_stdout}")
  message(STATUS "link stderr:\n${link_stderr}")
  message(FATAL_ERROR "failed to link differential parity runner")
endif()

set(harness_cmd "${harness_exe}" 128)

execute_process(
  COMMAND ${harness_cmd}
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
  message(STATUS "harness stdout:\n${run_stdout}")
  message(STATUS "harness stderr:\n${run_stderr}")
  message(FATAL_ERROR "differential parity harness reported mismatches")
endif()

set(min_random_iterations 128)
set(min_random_cases 10)
set(expected_directed_cases 0)
string(REGEX MATCH
  "PASS differential parity random_iterations=([0-9]+) random_cases=([0-9]+) directed_cases=([0-9]+)"
  parity_summary_line
  "${run_stdout}")
if(NOT parity_summary_line)
  message(FATAL_ERROR
    "failed to parse differential parity summary line from harness output")
endif()
set(observed_random_iterations "${CMAKE_MATCH_1}")
set(observed_random_cases "${CMAKE_MATCH_2}")
set(observed_directed_cases "${CMAKE_MATCH_3}")
if(observed_random_iterations LESS min_random_iterations)
  message(FATAL_ERROR
    "differential parity random-iteration regression: observed=${observed_random_iterations}, required>=${min_random_iterations}")
endif()
if(observed_random_cases LESS min_random_cases)
  message(FATAL_ERROR
    "differential parity random-case regression: observed=${observed_random_cases}, required>=${min_random_cases}")
endif()
if(NOT observed_directed_cases EQUAL expected_directed_cases)
  message(FATAL_ERROR
    "differential parity directed-case drift: observed=${observed_directed_cases}, expected=${expected_directed_cases}")
endif()

string(REGEX MATCH
  "PASS differential inventory random_cases=([0-9]+) directed_cases=([0-9]+)"
  inventory_summary_match
  "${run_stdout}")
if(NOT inventory_summary_match)
  message(FATAL_ERROR "missing differential parity inventory summary marker")
endif()
set(inventory_random_cases "${CMAKE_MATCH_1}")
set(inventory_directed_cases "${CMAKE_MATCH_2}")
if(NOT inventory_random_cases EQUAL observed_random_cases OR
   NOT inventory_directed_cases EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "differential parity inventory mismatch: inventory random=${inventory_random_cases}, "
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
    "differential parity random execution count mismatch: pass-lines=${observed_random_pass_lines}, "
    "summary random=${observed_random_cases}")
endif()

string(REGEX MATCHALL
  "PASS [A-Za-z0-9_]+ directed"
  directed_pass_lines
  "${run_stdout}")
list(LENGTH directed_pass_lines observed_directed_pass_lines)
if(NOT observed_directed_pass_lines EQUAL observed_directed_cases)
  message(FATAL_ERROR
    "differential parity directed execution count mismatch: pass-lines=${observed_directed_pass_lines}, "
    "summary directed=${observed_directed_cases}")
endif()

string(FIND "${run_stdout}" "INFO differential directed baseline directed_cases=0" directed_baseline_pos)
if(directed_baseline_pos EQUAL -1)
  message(FATAL_ERROR "missing differential parity directed baseline marker")
endif()

string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef summary_nonce)
set(summary_file "${OUT_DIR}/differential-summary.txt")
set(summary_tmp "${summary_file}.tmp-${summary_nonce}")
file(WRITE "${summary_tmp}" "${run_stdout}\n")
file(RENAME "${summary_tmp}" "${summary_file}")
message(STATUS "Differential parity summary:\n${run_stdout}")
