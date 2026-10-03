cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC FIXTURES_ROOT OUT_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()

if(NOT EXISTS "${FIXTURES_ROOT}")
  message(FATAL_ERROR "fixtures root not found: ${FIXTURES_ROOT}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --experimental-languages
      "${FIXTURES_ROOT}"
      --outdir "${OUT_DIR}"
      --ts-module "fixtures_dsdl_generated_ts"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "fixtures TypeScript generation failed")
endif()

foreach(required
    "${OUT_DIR}/package.json"
    "${OUT_DIR}/index.ts"
    "${OUT_DIR}/dsdl_runtime.ts")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "missing required generated file: ${required}")
  endif()
endforeach()

file(GLOB_RECURSE dsdl_files "${FIXTURES_ROOT}/*.dsdl")
list(LENGTH dsdl_files dsdl_count)

file(GLOB_RECURSE ts_files "${OUT_DIR}/*.ts")
set(type_ts_files "")
foreach(ts IN LISTS ts_files)
  get_filename_component(name "${ts}" NAME)
  if(NOT name STREQUAL "index.ts" AND NOT name STREQUAL "dsdl_runtime.ts")
    list(APPEND type_ts_files "${ts}")
  endif()
endforeach()
list(LENGTH type_ts_files type_ts_count)

if(NOT dsdl_count EQUAL type_ts_count)
  message(FATAL_ERROR
    "TypeScript type file count mismatch: dsdl=${dsdl_count}, generated=${type_ts_count}")
endif()

foreach(ts IN LISTS type_ts_files)
  file(READ "${ts}" ts_text)
  if(NOT ts_text MATCHES "\n  serialize\\(value: ")
    message(FATAL_ERROR "missing runtime serialize member in generated type module: ${ts}")
  endif()
  if(NOT ts_text MATCHES "\n  deserialize\\(bytes: ")
    message(FATAL_ERROR "missing runtime deserialize member in generated type module: ${ts}")
  endif()
  if(ts_text MATCHES "TypeScript runtime path is not yet available for this DSDL type")
    message(FATAL_ERROR
      "generated TypeScript type module contains runtime unsupported fallback signature: ${ts}")
  endif()
endforeach()

message(STATUS
  "fixtures TypeScript generation hardening check passed: ${dsdl_count} DSDL -> ${type_ts_count} TypeScript type files")
