cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC DSDLOPT UAVCAN_ROOT OUT_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()

if(NOT EXISTS "${DSDLOPT}")
  message(FATAL_ERROR "dsdl-opt executable not found: ${DSDLOPT}")
endif()

if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "uavcan root not found: ${UAVCAN_ROOT}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(input_mlir "${OUT_DIR}/uavcan.input.mlir")
set(lowered_mlir "${OUT_DIR}/uavcan.lowered.mlir")
set(lowered_optimized_mlir "${OUT_DIR}/uavcan.lowered.optimized.mlir")

execute_process(
  COMMAND
    "${DSDLC}" --target-language mlir
      "${UAVCAN_ROOT}"
  RESULT_VARIABLE mlir_result
  OUTPUT_FILE "${input_mlir}"
  ERROR_VARIABLE mlir_stderr
)
if(NOT mlir_result EQUAL 0)
  message(STATUS "dsdlc stderr:\n${mlir_stderr}")
  message(FATAL_ERROR "failed to generate full uavcan MLIR with dsdlc")
endif()

execute_process(
  COMMAND
    "${DSDLOPT}"
      "--pass-pipeline=builtin.module(lower-dsdl-serialization)"
      "${input_mlir}"
  RESULT_VARIABLE lower_result
  OUTPUT_FILE "${lowered_mlir}"
  ERROR_VARIABLE lower_stderr
)
if(NOT lower_result EQUAL 0)
  message(STATUS "dsdl-opt lower stderr:\n${lower_stderr}")
  message(FATAL_ERROR "full uavcan lower-dsdl-serialization pass failed")
endif()

execute_process(
  COMMAND
    "${DSDLOPT}"
      "--pass-pipeline=builtin.module(lower-dsdl-serialization,optimize-dsdl-lowered-serdes)"
      "${input_mlir}"
  RESULT_VARIABLE lower_optimized_result
  OUTPUT_FILE "${lowered_optimized_mlir}"
  ERROR_VARIABLE lower_optimized_stderr
)
if(NOT lower_optimized_result EQUAL 0)
  message(STATUS "dsdl-opt optimised lower stderr:\n${lower_optimized_stderr}")
  message(FATAL_ERROR
    "full uavcan optimized lower-dsdl-serialization pipeline failed")
endif()

file(READ "${lowered_mlir}" lowered_text)
file(READ "${lowered_optimized_mlir}" lowered_optimized_text)
foreach(required
    "llvmdsdl.lowered_contract_version = 2 : i64"
    "llvmdsdl.lowered_contract_producer = \"lower-dsdl-exec\""
    "lowered"
    "lowered_step_count ="
    "lowered_field_count ="
    "step_index = 0 : i64"
    "lowered_bits =")
  string(FIND "${lowered_text}" "${required}" hit_pos)
  if(hit_pos EQUAL -1)
    message(FATAL_ERROR
      "expected lowered output marker not found in full uavcan lowering: ${required}")
  endif()
endforeach()
foreach(required
    "llvmdsdl.lowered_contract_version = 2 : i64"
    "llvmdsdl.lowered_contract_producer = \"lower-dsdl-exec\""
    "lowered"
    "lowered_step_count ="
    "lowered_field_count ="
    "step_index = 0 : i64"
    "lowered_bits =")
  string(FIND "${lowered_optimized_text}" "${required}" hit_pos)
  if(hit_pos EQUAL -1)
    message(FATAL_ERROR
      "expected optimized lowered output marker not found in full uavcan lowering: ${required}")
  endif()
endforeach()

string(FIND "${lowered_text}" "dsdl.align {bits = 1 : i32" align_noop_pos)
if(NOT align_noop_pos EQUAL -1)
  message(FATAL_ERROR
    "no-op alignment op survived full uavcan lower-dsdl-serialization pass")
endif()
string(FIND "${lowered_optimized_text}" "dsdl.align {bits = 1 : i32" align_noop_optimized_pos)
if(NOT align_noop_optimized_pos EQUAL -1)
  message(FATAL_ERROR
    "no-op alignment op survived full uavcan optimized lower-dsdl-serialization pass")
endif()

message(STATUS "full uavcan MLIR lowering check passed (baseline + optimised pipelines)")
