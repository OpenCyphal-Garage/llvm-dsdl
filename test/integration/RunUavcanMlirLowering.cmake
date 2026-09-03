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
set(converted_mlir "${OUT_DIR}/uavcan.converted.mlir")
set(converted_optimized_mlir "${OUT_DIR}/uavcan.converted.optimized.mlir")
set(converted_optimized_again_mlir "${OUT_DIR}/uavcan.converted.optimized-again.mlir")

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
  message(STATUS "dsdl-opt optimized lower stderr:\n${lower_optimized_stderr}")
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

execute_process(
  COMMAND
    "${DSDLOPT}"
      "--pass-pipeline=builtin.module(lower-dsdl-serialization,build-dsdl-plan-bodies,convert-dsdl-to-emitc)"
      "${input_mlir}"
  RESULT_VARIABLE convert_result
  OUTPUT_FILE "${converted_mlir}"
  ERROR_VARIABLE convert_stderr
)
if(NOT convert_result EQUAL 0)
  message(STATUS "dsdl-opt convert stderr:\n${convert_stderr}")
  message(FATAL_ERROR "full uavcan convert-dsdl-to-emitc pass failed")
endif()

execute_process(
  COMMAND
    "${DSDLOPT}"
      "--pass-pipeline=builtin.module(lower-dsdl-serialization,optimize-dsdl-lowered-serdes,build-dsdl-plan-bodies,convert-dsdl-to-emitc)"
      "${input_mlir}"
  RESULT_VARIABLE convert_optimized_result
  OUTPUT_FILE "${converted_optimized_mlir}"
  ERROR_VARIABLE convert_optimized_stderr
)
if(NOT convert_optimized_result EQUAL 0)
  message(STATUS "dsdl-opt optimized convert stderr:\n${convert_optimized_stderr}")
  message(FATAL_ERROR
    "full uavcan optimized convert-dsdl-to-emitc pass failed")
endif()

file(READ "${converted_mlir}" converted_text)
file(READ "${converted_optimized_mlir}" converted_optimized_text)
foreach(required
    "call @llvmdsdl_plan_capacity_check__"
    "int8_t llvmdsdl_plan_capacity_check__"
    "int64_t llvmdsdl_plan_union_tag__"
    "int64_t llvmdsdl_plan_scalar_unsigned__"
    "int64_t llvmdsdl_plan_scalar_signed__"
    "double llvmdsdl_plan_scalar_float__"
    "int64_t llvmdsdl_plan_array_length_prefix__"
    "int8_t llvmdsdl_plan_validate_array_length__"
    "int8_t llvmdsdl_plan_validate_delimiter_header__")
  string(FIND "${converted_text}" "${required}" hit_pos)
  if(hit_pos EQUAL -1)
    message(FATAL_ERROR
      "expected converted output marker not found in full uavcan convert: ${required}")
  endif()
endforeach()
foreach(required
    "call @llvmdsdl_plan_capacity_check__"
    "int8_t llvmdsdl_plan_capacity_check__"
    "int64_t llvmdsdl_plan_union_tag__"
    "int64_t llvmdsdl_plan_scalar_unsigned__"
    "int64_t llvmdsdl_plan_scalar_signed__"
    "double llvmdsdl_plan_scalar_float__"
    "int64_t llvmdsdl_plan_array_length_prefix__"
    "int8_t llvmdsdl_plan_validate_array_length__"
    "int8_t llvmdsdl_plan_validate_delimiter_header__")
  string(FIND "${converted_optimized_text}" "${required}" hit_pos)
  if(hit_pos EQUAL -1)
    message(FATAL_ERROR
      "expected optimized converted output marker not found in full uavcan convert: ${required}")
  endif()
endforeach()

execute_process(
  COMMAND
    "${DSDLOPT}"
      "--pass-pipeline=builtin.module(lower-dsdl-serialization,optimize-dsdl-lowered-serdes,build-dsdl-plan-bodies,convert-dsdl-to-emitc)"
      "${input_mlir}"
  RESULT_VARIABLE convert_optimized_again_result
  OUTPUT_FILE "${converted_optimized_again_mlir}"
  ERROR_VARIABLE convert_optimized_again_stderr
)
if(NOT convert_optimized_again_result EQUAL 0)
  message(STATUS
    "dsdl-opt optimized convert (repeat) stderr:\n${convert_optimized_again_stderr}")
  message(FATAL_ERROR
    "full uavcan optimized convert-dsdl-to-emitc repeat pass failed")
endif()

file(SHA256 "${converted_optimized_mlir}" converted_optimized_sha)
file(SHA256 "${converted_optimized_again_mlir}" converted_optimized_again_sha)
if(NOT converted_optimized_sha STREQUAL converted_optimized_again_sha)
  message(FATAL_ERROR
    "full uavcan optimized convert pipeline is not deterministic across repeated runs")
endif()

message(STATUS "full uavcan MLIR lowering + convert check passed (baseline + optimized pipelines)")
