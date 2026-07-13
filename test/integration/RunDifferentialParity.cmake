# Differential parity vs Nunavut — CORROBORATION, not an oracle.
#
# The Cyphal Specification is the truth; spec/dafny/CyphalSerdes.dfy is the
# machine-checked oracle (see its Authority hierarchy). Nunavut is a pinned peer
# implementation: byte agreement here corroborates both implementations, and a
# mismatch is an investigation adjudicated by the specification — it is as likely
# to be a Nunavut defect as ours. The 10 cases cover every wire-shape class;
# coverage expansion was deliberately retired (2026-07-12, see the roadmap P0
# entry) in favor of spec-derived verification.
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
set(ours_template "${SOURCE_ROOT}/test/integration/DifferentialParityOurs.c.in")
set(nv_template "${SOURCE_ROOT}/test/integration/DifferentialParityNunavut.c.in")
set(abi_header "${SOURCE_ROOT}/test/integration/DifferentialParityABI.h")
foreach(path "${main_template}" "${ours_template}" "${nv_template}" "${abi_header}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Differential harness input missing: ${path}")
  endif()
endforeach()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

set(ours_out "${OUT_DIR}/ours")
set(dsdl_out "${OUT_DIR}/nunavut")
set(build_out "${OUT_DIR}/build")
file(MAKE_DIRECTORY "${ours_out}")
file(MAKE_DIRECTORY "${dsdl_out}")
file(MAKE_DIRECTORY "${build_out}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language c
      "${UAVCAN_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${ours_out}"
  RESULT_VARIABLE ours_result
  OUTPUT_VARIABLE ours_stdout
  ERROR_VARIABLE ours_stderr
)
if(NOT ours_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${ours_stdout}")
  message(STATUS "dsdlc stderr:\n${ours_stderr}")
  message(FATAL_ERROR "failed to generate llvm-dsdl C output")
endif()

set(pythonpath "${PYDSDL_REPO}:${NUNAVUT_REPO}/src")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "PYTHONPATH=${pythonpath}"
      "${PYTHON_EXECUTABLE}" -m nunavut
      --jobs 1
      --target-language c
      --outdir "${dsdl_out}"
      --lookup-dir "${UAVCAN_ROOT}"
      "${UAVCAN_ROOT}:node/7509.Heartbeat.1.0.dsdl"
      "${UAVCAN_ROOT}:node/435.ExecuteCommand.1.3.dsdl"
      "${UAVCAN_ROOT}:register/Value.1.0.dsdl"
      "${UAVCAN_ROOT}:metatransport/can/Frame.0.2.dsdl"
      "${UAVCAN_ROOT}:primitive/array/Real32.1.0.dsdl"
      "${UAVCAN_ROOT}:node/port/SubjectIDList.1.0.dsdl"
      "${UAVCAN_ROOT}:pnp/8165.NodeIDAllocationData.2.0.dsdl"
      "${UAVCAN_ROOT}:diagnostic/8184.Record.1.1.dsdl"
      "${UAVCAN_ROOT}:time/SynchronizedTimestamp.1.0.dsdl"
  RESULT_VARIABLE nunavut_result
  OUTPUT_VARIABLE nunavut_stdout
  ERROR_VARIABLE nunavut_stderr
)
if(NOT nunavut_result EQUAL 0)
  message(STATUS "nunavut stdout:\n${nunavut_stdout}")
  message(STATUS "nunavut stderr:\n${nunavut_stderr}")
  message(FATAL_ERROR "failed to generate nunavut reference C output")
endif()

set(OURS_HEARTBEAT_HEADER "${ours_out}/uavcan/node/Heartbeat_1_0.h")
set(OURS_EXECUTECOMMAND_HEADER "${ours_out}/uavcan/node/ExecuteCommand_1_3.h")
set(OURS_VALUE_HEADER "${ours_out}/uavcan/register/Value_1_0.h")
set(OURS_FRAME_HEADER "${ours_out}/uavcan/metatransport/can/Frame_0_2.h")
set(OURS_REAL32_HEADER "${ours_out}/uavcan/primitive/array/Real32_1_0.h")
set(OURS_SUBJECTIDLIST_HEADER "${ours_out}/uavcan/node/port/SubjectIDList_1_0.h")
set(OURS_NODEIDALLOC_HEADER "${ours_out}/uavcan/pnp/NodeIDAllocationData_2_0.h")
set(OURS_DIAGRECORD_HEADER "${ours_out}/uavcan/diagnostic/Record_1_1.h")
set(OURS_SYNCTIMESTAMP_HEADER "${ours_out}/uavcan/time/SynchronizedTimestamp_1_0.h")

set(NV_HEARTBEAT_HEADER "${dsdl_out}/uavcan/node/Heartbeat_1_0.h")
set(NV_EXECUTECOMMAND_HEADER "${dsdl_out}/uavcan/node/ExecuteCommand_1_3.h")
set(NV_VALUE_HEADER "${dsdl_out}/uavcan/_register/Value_1_0.h")
set(NV_FRAME_HEADER "${dsdl_out}/uavcan/metatransport/can/Frame_0_2.h")
set(NV_REAL32_HEADER "${dsdl_out}/uavcan/primitive/array/Real32_1_0.h")
set(NV_SUBJECTIDLIST_HEADER "${dsdl_out}/uavcan/node/port/SubjectIDList_1_0.h")
set(NV_NODEIDALLOC_HEADER "${dsdl_out}/uavcan/pnp/NodeIDAllocationData_2_0.h")
set(NV_DIAGRECORD_HEADER "${dsdl_out}/uavcan/diagnostic/Record_1_1.h")
set(NV_SYNCTIMESTAMP_HEADER "${dsdl_out}/uavcan/time/SynchronizedTimestamp_1_0.h")

foreach(header
    "${OURS_HEARTBEAT_HEADER}"
    "${OURS_EXECUTECOMMAND_HEADER}"
    "${OURS_VALUE_HEADER}"
    "${OURS_FRAME_HEADER}"
    "${OURS_REAL32_HEADER}"
    "${OURS_SUBJECTIDLIST_HEADER}"
    "${OURS_NODEIDALLOC_HEADER}"
    "${OURS_DIAGRECORD_HEADER}"
    "${OURS_SYNCTIMESTAMP_HEADER}"
    "${NV_HEARTBEAT_HEADER}"
    "${NV_EXECUTECOMMAND_HEADER}"
    "${NV_VALUE_HEADER}"
    "${NV_FRAME_HEADER}"
    "${NV_REAL32_HEADER}"
    "${NV_SUBJECTIDLIST_HEADER}"
    "${NV_NODEIDALLOC_HEADER}"
    "${NV_DIAGRECORD_HEADER}"
    "${NV_SYNCTIMESTAMP_HEADER}")
  if(NOT EXISTS "${header}")
    message(FATAL_ERROR "expected generated header missing: ${header}")
  endif()
endforeach()

set(main_c "${build_out}/differential_parity_main.c")
set(ours_case_c "${build_out}/differential_parity_ours.c")
set(nv_case_c "${build_out}/differential_parity_nunavut.c")
configure_file("${main_template}" "${main_c}" @ONLY)
configure_file("${ours_template}" "${ours_case_c}" @ONLY)
configure_file("${nv_template}" "${nv_case_c}" @ONLY)

file(GLOB_RECURSE ours_c_sources "${ours_out}/*.c")
list(LENGTH ours_c_sources ours_c_count)
if(ours_c_count EQUAL 0)
  message(FATAL_ERROR "no generated llvm-dsdl C implementation files under ${ours_out}")
endif()

set(main_obj "${build_out}/differential_parity_main.o")
set(ours_case_obj "${build_out}/differential_parity_ours.o")
set(nv_case_obj "${build_out}/differential_parity_nunavut.o")
set(generated_obj_dir "${build_out}/generated-obj")
file(MAKE_DIRECTORY "${generated_obj_dir}")

execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
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

execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${SOURCE_ROOT}/test/integration"
      -I "${ours_out}"
      -c "${ours_case_c}"
      -o "${ours_case_obj}"
  RESULT_VARIABLE cc_result
  OUTPUT_VARIABLE cc_stdout
  ERROR_VARIABLE cc_stderr
)
if(NOT cc_result EQUAL 0)
  message(STATUS "compiler stdout:\n${cc_stdout}")
  message(STATUS "compiler stderr:\n${cc_stderr}")
  message(FATAL_ERROR "failed to compile differential parity llvm-dsdl unit")
endif()

execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${SOURCE_ROOT}/test/integration"
      -I "${dsdl_out}"
      -c "${nv_case_c}"
      -o "${nv_case_obj}"
  RESULT_VARIABLE cc_result
  OUTPUT_VARIABLE cc_stdout
  ERROR_VARIABLE cc_stderr
)
if(NOT cc_result EQUAL 0)
  message(STATUS "compiler stdout:\n${cc_stdout}")
  message(STATUS "compiler stderr:\n${cc_stderr}")
  message(FATAL_ERROR "failed to compile differential parity nunavut unit")
endif()

set(generated_objs "")
set(index 0)
foreach(src IN LISTS ours_c_sources)
  math(EXPR index "${index} + 1")
  set(obj "${generated_obj_dir}/generated_${index}.o")
  execute_process(
    COMMAND
      "${C_COMPILER}"
        -std=c11
        -Wall
        -Wextra
        -Werror
        -I "${ours_out}"
        -c "${src}"
        -o "${obj}"
    RESULT_VARIABLE cc_result
    OUTPUT_VARIABLE cc_stdout
    ERROR_VARIABLE cc_stderr
  )
  if(NOT cc_result EQUAL 0)
    message(STATUS "failed source: ${src}")
    message(STATUS "compiler stdout:\n${cc_stdout}")
    message(STATUS "compiler stderr:\n${cc_stderr}")
    message(FATAL_ERROR "failed to compile generated llvm-dsdl C implementation")
  endif()
  list(APPEND generated_objs "${obj}")
endforeach()

set(harness_exe "${build_out}/differential_parity_runner")
execute_process(
  COMMAND
    "${C_COMPILER}"
      "${main_obj}"
      "${ours_case_obj}"
      "${nv_case_obj}"
      ${generated_objs}
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
