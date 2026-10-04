cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR TSC_EXECUTABLE)
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

if(NOT EXISTS "${TSC_EXECUTABLE}")
  message(FATAL_ERROR "tsc executable not found: ${TSC_EXECUTABLE}")
endif()

if(NOT DEFINED TS_RUNTIME_SPECIALIZATION OR "${TS_RUNTIME_SPECIALIZATION}" STREQUAL "")
  set(TS_RUNTIME_SPECIALIZATION "portable")
endif()
if(NOT "${TS_RUNTIME_SPECIALIZATION}" STREQUAL "portable" AND
   NOT "${TS_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
  message(FATAL_ERROR "Invalid TS_RUNTIME_SPECIALIZATION value: ${TS_RUNTIME_SPECIALIZATION}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --experimental-languages
      "${UAVCAN_ROOT}"
      --outdir "${OUT_DIR}"
      --ts-module "uavcan_dsdl_generated_ts"
      --ts-runtime-specialization "${TS_RUNTIME_SPECIALIZATION}"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "uavcan TypeScript generation failed")
endif()

file(WRITE
  "${OUT_DIR}/tsconfig.json"
  "{\n"
  "  \"compilerOptions\": {\n"
  "    \"target\": \"ES2022\",\n"
  "    \"module\": \"ES2022\",\n"
  "    \"moduleResolution\": \"Node\",\n"
  "    \"strict\": true,\n"
  "    \"noEmit\": true,\n"
  "    \"skipLibCheck\": true\n"
  "  },\n"
  "  \"include\": [\"./**/*.ts\"]\n"
  "}\n"
)

execute_process(
  COMMAND "${TSC_EXECUTABLE}" -p "${OUT_DIR}/tsconfig.json" --pretty false
  WORKING_DIRECTORY "${OUT_DIR}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "generated uavcan TypeScript module failed type-check")
endif()

message(STATUS "uavcan TypeScript type-check gate passed")
