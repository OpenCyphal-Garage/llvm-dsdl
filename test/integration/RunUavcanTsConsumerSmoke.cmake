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

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(gen_dir "${OUT_DIR}/generated")
file(MAKE_DIRECTORY "${gen_dir}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --versioned-type-names
      "${UAVCAN_ROOT}"
      --outdir "${gen_dir}"
      --ts-module "uavcan_dsdl_generated_ts"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "uavcan TypeScript generation failed")
endif()

file(READ "${gen_dir}/index.ts" ts_index)
foreach(required_alias
    "uavcan_node_heartbeat_1_0"
    "uavcan_primitive_empty_1_0")
  if(NOT ts_index MATCHES "export \\* as ${required_alias} from ")
    message(FATAL_ERROR "Expected root TypeScript index.ts to export alias: ${required_alias}")
  endif()
endforeach()

file(WRITE
  "${gen_dir}/consumer_smoke.ts"
  "import { uavcan_node_heartbeat_1_0, uavcan_primitive_empty_1_0 } from \"./index\";\n"
  "\n"
  "const heartbeatName: string = uavcan_node_heartbeat_1_0.DSDL_FULL_NAME;\n"
  "const emptyName: string = uavcan_primitive_empty_1_0.DSDL_FULL_NAME;\n"
  "const heartbeatMajor: number = uavcan_node_heartbeat_1_0.DSDL_VERSION_MAJOR;\n"
  "\n"
  "export const smokeSummary: string = `${heartbeatName}:${emptyName}:${heartbeatMajor}`;\n"
)

file(WRITE
  "${gen_dir}/tsconfig-smoke.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${gen_dir}/tsconfig-smoke.json" --pretty false
  WORKING_DIRECTORY "${gen_dir}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "generated uavcan TypeScript consumer smoke compile failed")
endif()

message(STATUS "uavcan TypeScript consumer smoke check passed")
